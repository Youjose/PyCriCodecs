/**
 * @file cpk_reader.cpp
 * @brief CPK archive reader.
 *
 * Parsing behavior is checked against official CRI CPK Maker outputs,
 * UTF-backed section layouts, and CRILAYLA-compressed sample data. C++23
 * reader implementation by Youjose.
 */

#include "cpk_container.hpp"
#include "crilayla.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "../utilities/flat_unordered_map.hpp"
#include "../utilities/numeric.hpp"
#include "../utilities/text_encoding.hpp"

namespace cricodecs::cpk {

namespace {

constexpr uint64_t root_chunk_size = 0x800;
constexpr uint32_t chunk_encrypted_flag = 0xFF;

using util::align_up;

template <unsigned Shift>
void stable_mode0_radix_pass(
    const CpkEntry* files,
    const size_t* input,
    size_t* output,
    size_t count)
{
    std::array<size_t, 256> offsets{};
    for (size_t position = 0; position < count; ++position) {
        const size_t index = input[position];
        ++offsets[(files[index].id >> Shift) & 0xFFu];
    }
    size_t position = 0;
    for (size_t& offset : offsets) {
        const size_t bucket_size = offset;
        offset = position;
        position += bucket_size;
    }
    for (size_t input_position = 0; input_position < count; ++input_position) {
        const size_t index = input[input_position];
        output[offsets[(files[index].id >> Shift) & 0xFFu]++] = index;
    }
}

[[nodiscard]] std::vector<size_t> stable_mode0_id_order(std::span<const CpkEntry> files) {
    std::vector<size_t> order(files.size());
    std::iota(order.begin(), order.end(), size_t{0});
    if (files.size() < 128) {
        std::stable_sort(order.begin(), order.end(), [&](size_t lhs, size_t rhs) {
            return files[lhs].id < files[rhs].id;
        });
        return order;
    }

    std::vector<size_t> scratch(files.size());
    stable_mode0_radix_pass<0>(files.data(), order.data(), scratch.data(), files.size());
    stable_mode0_radix_pass<8>(files.data(), scratch.data(), order.data(), files.size());
    return order;
}

template<typename T>
std::optional<T> value_to_unsigned(const utf::Value& value) {
    auto convert = []<typename U>(U raw) -> std::optional<T> {
        if constexpr (std::same_as<U, std::monostate> ||
                      std::same_as<U, std::string> ||
                      std::same_as<U, std::vector<uint8_t>> ||
                      std::same_as<U, utf::DataRef> ||
                      std::same_as<U, utf::GUID>) {
            return std::nullopt;
        } else if constexpr (std::floating_point<U>) {
            return std::nullopt;
        } else if constexpr (std::signed_integral<U>) {
            if (raw < 0) {
                return std::nullopt;
            }
            using UnsignedSource = std::make_unsigned_t<U>;
            const auto converted = static_cast<UnsignedSource>(raw);
            if (converted > static_cast<UnsignedSource>(std::numeric_limits<T>::max())) {
                return std::nullopt;
            }
            return static_cast<T>(converted);
        } else if constexpr (std::unsigned_integral<U>) {
            if (raw > std::numeric_limits<T>::max()) {
                return std::nullopt;
            }
            return static_cast<T>(raw);
        } else {
            return std::nullopt;
        }
    };

    return std::visit(convert, value);
}

template<typename T>
std::expected<std::optional<T>, std::string> get_optional_unsigned_at(
    const utf::UtfTable& table,
    uint32_t row,
    int column_index,
    std::string_view column_name
) {
    if (column_index < 0) {
        return std::optional<T>{};
    }

    auto value = table.get_value(row, static_cast<uint32_t>(column_index));
    if (!value) {
        return std::unexpected(value.error());
    }

    if (std::holds_alternative<std::monostate>(*value)) {
        return std::optional<T>{};
    }

    auto converted = value_to_unsigned<T>(*value);
    if (!converted.has_value()) {
        return std::unexpected("CPK UTF column has an unexpected numeric type: " + std::string(column_name));
    }

    return converted;
}

template<typename T>
std::expected<std::optional<T>, std::string> get_optional_unsigned(
    const utf::UtfTable& table,
    uint32_t row,
    std::string_view column_name
) {
    return get_optional_unsigned_at<T>(table, row, table.find_column(column_name), column_name);
}

template<typename T>
std::expected<T, std::string> get_required_unsigned_at(
    const utf::UtfTable& table,
    uint32_t row,
    int column_index,
    std::string_view column_name
) {
    auto value = get_optional_unsigned_at<T>(table, row, column_index, column_name);
    if (!value) {
        return std::unexpected(value.error());
    }
    if (!value->has_value()) {
        return std::unexpected("CPK UTF column is missing: " + std::string(column_name));
    }
    return **value;
}

template<typename T>
std::expected<T, std::string> get_required_unsigned(
    const utf::UtfTable& table,
    uint32_t row,
    std::string_view column_name
) {
    return get_required_unsigned_at<T>(table, row, table.find_column(column_name), column_name);
}

std::expected<std::string, std::string> get_optional_string_at(
    const utf::UtfTable& table,
    uint32_t row,
    int column_index,
    std::string_view column_name
) {
    if (column_index < 0) {
        return std::string{};
    }

    auto value = table.get_value(row, static_cast<uint32_t>(column_index));
    if (!value) {
        return std::unexpected(value.error());
    }

    if (std::holds_alternative<std::monostate>(*value)) {
        return std::string{};
    }

    if (const auto* text = std::get_if<std::string>(&*value)) {
        return *text;
    }

    return std::unexpected("CPK UTF column has an unexpected string type: " + std::string(column_name));
}

std::expected<std::string, std::string> get_optional_string(
    const utf::UtfTable& table,
    uint32_t row,
    std::string_view column_name
) {
    return get_optional_string_at(table, row, table.find_column(column_name), column_name);
}

std::expected<std::string, std::string> decode_cri_string(
    std::string_view raw,
    const text::EncodingOptions& encoding,
    std::string_view context
) {
    auto decoded = text::decode_to_utf8(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()),
        encoding
    );
    if (!decoded) {
        return std::unexpected(std::string(context) + ": " + decoded.error());
    }
    return *decoded;
}

void decrypt_utf_payload(std::vector<uint8_t>& payload) {
    uint64_t m = 0x655F;
    constexpr uint64_t t = 0x4115;
    for (auto& byte : payload) {
        byte ^= static_cast<uint8_t>(m & 0xFF);
        m = (m * t) & 0xFFFFFFFFull;
    }
}

std::filesystem::path with_appended_suffix(
    const std::filesystem::path& path,
    std::string_view suffix
) {
    const std::filesystem::path parent = path.parent_path();
    const std::string stem = path.stem().string();
    const std::string ext = path.extension().string();
    const std::string filename = stem.empty() && ext.empty()
        ? std::string(suffix)
        : stem + std::string(suffix) + ext;

    return parent / filename;
}

std::filesystem::path disambiguate_output_path(
    const CpkEntry& entry,
    util::flat_unordered_set<std::string>& used_paths
) {
    const auto raw_path = entry.full_path();
    const auto raw_key = raw_path.generic_string();
    if (used_paths.insert(raw_key).second) {
        return raw_path;
    }

    const auto id_path = with_appended_suffix(raw_path, "_" + std::to_string(entry.id));
    const auto id_key = id_path.generic_string();
    if (used_paths.insert(id_key).second) {
        return id_path;
    }

    const auto indexed_path = with_appended_suffix(
        raw_path,
        "_" + std::to_string(entry.id) + "_" + std::to_string(entry.toc_index)
    );
    used_paths.insert(indexed_path.generic_string());
    return indexed_path;
}

std::expected<utf::UtfTable, std::string> load_nested_utf(std::span<const uint8_t> data) {
    auto table = utf::UtfTable::load(data);
    if (!table) {
        return std::unexpected(table.error());
    }
    return *table;
}

CpkPreset decode_declared_preset(uint32_t raw_mode) {
    switch (raw_mode) {
        case 0:
            return CpkPreset::Id;
        case 1:
            return CpkPreset::Filename;
        case 2:
            return CpkPreset::FilenameId;
        case 3:
            return CpkPreset::FilenameGroup;
        case 4:
            return CpkPreset::IdGroup;
        case 5:
            return CpkPreset::FilenameIdGroup;
        default:
            return CpkPreset::Custom;
    }
}

std::expected<CpkMode, std::string> detect_layout_mode(bool has_toc, bool has_itoc, bool has_gtoc) {
    if (has_toc) {
        return has_itoc
            ? (has_gtoc ? CpkMode::Mode3 : CpkMode::Mode2)
            : CpkMode::Mode1;
    }
    if (has_itoc) {
        return CpkMode::Mode0;
    }
    return std::unexpected("CPK archive has neither TOC nor ITOC");
}

} // namespace

std::expected<Cpk, std::string> Cpk::load(
    const std::filesystem::path& path,
    const text::EncodingOptions& encoding
) {
    Cpk archive;
    archive.set_encoding(encoding);
    if (auto result = archive.load_from_path(path); !result) {
        return std::unexpected(result.error());
    }
    return archive;
}

std::expected<Cpk, std::string> Cpk::load(
    std::span<const uint8_t> data,
    const text::EncodingOptions& encoding
) {
    Cpk archive;
    archive.set_encoding(encoding);
    if (auto result = archive.load_from_bytes(data); !result) {
        return std::unexpected(result.error());
    }
    return archive;
}

std::expected<Cpk, std::string> Cpk::load(
    std::vector<uint8_t>&& data,
    const text::EncodingOptions& encoding
) {
    Cpk archive;
    archive.set_encoding(encoding);
    if (auto result = archive.load_from_bytes(std::move(data)); !result) {
        return std::unexpected(result.error());
    }
    return archive;
}

Cpk Cpk::create(const CpkOptions& options) {
    Cpk archive;
    archive.m_options = options;
    archive.m_align = options.align;
    archive.m_preset = options.preset;
    archive.m_has_declared_preset = options.preset != CpkPreset::Custom;
    archive.m_declared_preset = options.preset;
    archive.m_dirty = true;
    return archive;
}

std::expected<void, std::string> Cpk::load_from_path(const std::filesystem::path& path) {
    m_source_path = path;
    m_reader = io::reader{};
    m_owned_archive_bytes.clear();
    if (auto result = m_reader.open(path); !result) {
        return std::unexpected(std::string(result.error()));
    }
    return parse();
}

std::expected<void, std::string> Cpk::load_from_bytes(std::span<const uint8_t> data) {
    return load_owned_bytes(std::vector<uint8_t>(data.begin(), data.end()));
}

std::expected<void, std::string> Cpk::load_from_bytes(std::vector<uint8_t>&& data) {
    return load_owned_bytes(std::move(data));
}

std::expected<void, std::string> Cpk::load_owned_bytes(std::vector<uint8_t>&& data) {
    m_source_path.clear();
    m_reader = io::reader{};
    m_owned_archive_bytes = std::move(data);
    if (auto result = m_reader.open(std::span<const uint8_t>(m_owned_archive_bytes)); !result) {
        return std::unexpected(std::string(result.error()));
    }
    return parse();
}

void Cpk::add_file(
    const std::filesystem::path& local_path,
    const std::string& cpk_path,
    bool compress,
    std::optional<uint32_t> id
) {
    CpkEntry entry;
    normalize_entry_path(entry, cpk_path);
    entry.id = id.value_or(0);
    entry.request_compress = compress;
    entry.is_compressed = compress;

    m_files.push_back(std::move(entry));
    EntrySource source;
    source.kind = EntrySourceKind::FilePath;
    source.path = local_path;
    source.explicit_id = id;
    m_sources.push_back(std::move(source));
    m_dirty = true;
}

void Cpk::add_bytes(
    std::span<const uint8_t> bytes,
    const std::string& cpk_path,
    bool compress,
    std::optional<uint32_t> id
) {
    CpkEntry entry;
    normalize_entry_path(entry, cpk_path);
    entry.id = id.value_or(0);
    entry.request_compress = compress;
    entry.is_compressed = compress;

    EntrySource source;
    source.kind = EntrySourceKind::OwnedBytes;
    source.bytes.assign(bytes.begin(), bytes.end());
    source.explicit_id = id;

    m_files.push_back(std::move(entry));
    m_sources.push_back(std::move(source));
    m_dirty = true;
}

std::expected<void, std::string> Cpk::remove(size_t index) {
    if (index >= m_files.size()) {
        return std::unexpected("CPK file index out of range");
    }
    m_files.erase(m_files.begin() + static_cast<std::ptrdiff_t>(index));
    m_sources.erase(m_sources.begin() + static_cast<std::ptrdiff_t>(index));
    m_dirty = true;
    return {};
}

std::expected<void, std::string> Cpk::move_file(size_t from_index, size_t to_index) {
    if (from_index >= m_files.size() || to_index >= m_files.size()) {
        return std::unexpected("CPK file index out of range");
    }
    if (from_index == to_index) {
        return {};
    }

    auto file = std::move(m_files[from_index]);
    auto source = std::move(m_sources[from_index]);
    m_files.erase(m_files.begin() + static_cast<std::ptrdiff_t>(from_index));
    m_sources.erase(m_sources.begin() + static_cast<std::ptrdiff_t>(from_index));
    m_files.insert(m_files.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(file));
    m_sources.insert(m_sources.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(source));
    m_dirty = true;
    return {};
}

std::expected<void, std::string> Cpk::rename(size_t index, const std::string& cpk_path) {
    if (index >= m_files.size()) {
        return std::unexpected("CPK file index out of range");
    }
    normalize_entry_path(m_files[index], cpk_path);
    m_dirty = true;
    return {};
}

std::expected<void, std::string> Cpk::set_dirname(size_t index, const std::string& dirname) {
    if (index >= m_files.size()) {
        return std::unexpected("CPK file index out of range");
    }

    const auto normalized = std::filesystem::path(dirname).lexically_normal().generic_string();
    auto& entry = m_files[index];
    entry.dirname = normalized == "." ? std::string{} : normalized;
    entry.dirname_raw.clear();
    m_dirty = true;
    return {};
}

std::expected<void, std::string> Cpk::set_filename(size_t index, const std::string& filename) {
    if (index >= m_files.size()) {
        return std::unexpected("CPK file index out of range");
    }
    if (filename.empty() || filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
        return std::unexpected("CPK filename must be a non-empty leaf name without path separators");
    }

    auto& entry = m_files[index];
    entry.filename = filename;
    entry.filename_raw.clear();
    m_dirty = true;
    return {};
}

std::expected<void, std::string> Cpk::set_request_compress(size_t index, bool compress) {
    if (index >= m_files.size()) {
        return std::unexpected("CPK file index out of range");
    }
    m_files[index].request_compress = compress;
    m_dirty = true;
    return {};
}

void Cpk::set_all_request_compress(bool compress) noexcept {
    for (auto& entry : m_files) {
        entry.request_compress = compress;
    }
    m_dirty = true;
}

std::expected<void, std::string> Cpk::replace_file(
    size_t index,
    const std::filesystem::path& local_path,
    std::optional<bool> compress
) {
    if (index >= m_files.size()) {
        return std::unexpected("CPK file index out of range");
    }
    auto& entry = m_files[index];
    auto& source = m_sources[index];
    source = EntrySource{};
    source.kind = EntrySourceKind::FilePath;
    source.path = local_path;
    source.explicit_id = entry.id;
    if (compress.has_value()) {
        entry.request_compress = *compress;
    }
    m_dirty = true;
    return {};
}

std::expected<void, std::string> Cpk::replace_bytes(
    size_t index,
    std::span<const uint8_t> bytes,
    std::optional<bool> compress
) {
    if (index >= m_files.size()) {
        return std::unexpected("CPK file index out of range");
    }
    auto& entry = m_files[index];
    auto& source = m_sources[index];
    source = EntrySource{};
    source.kind = EntrySourceKind::OwnedBytes;
    source.explicit_id = entry.id;
    source.bytes.assign(bytes.begin(), bytes.end());
    if (compress.has_value()) {
        entry.request_compress = *compress;
    }
    m_dirty = true;
    return {};
}

std::expected<std::span<const uint8_t>, std::string> Cpk::packed_entry_span(const CpkEntry& entry) const {
    if (entry.file_offset > m_reader.size()) {
        return std::unexpected("CPK entry offset is out of range");
    }
    if (entry.file_size > static_cast<uint64_t>(m_reader.size() - entry.file_offset)) {
        return std::unexpected("CPK entry data exceeds the archive size");
    }

    return m_reader.subspan(
        static_cast<size_t>(entry.file_offset),
        static_cast<size_t>(entry.file_size)
    );
}

std::expected<std::vector<uint8_t>, std::string> Cpk::extract_to_memory(const CpkEntry& entry) const {
    auto packed_span = packed_entry_span(entry);
    if (!packed_span) {
        return std::unexpected(packed_span.error());
    }

    if (!entry.is_compressed) {
        return std::vector<uint8_t>(packed_span->begin(), packed_span->end());
    }

    auto decompressed = cricodecs::crilayla::decompress(*packed_span);
    if (!decompressed) {
        return std::unexpected("CPK extract failed: could not decompress CRILAYLA payload: " + decompressed.error());
    }
    return *decompressed;
}

std::expected<void, std::string> Cpk::write_entry_to_file(
    const CpkEntry& entry,
    const std::filesystem::path& output_path
) const {
    auto packed_span = packed_entry_span(entry);
    if (!packed_span) {
        return std::unexpected(packed_span.error());
    }

    std::vector<uint8_t> decompressed;
    std::span<const uint8_t> output_bytes = *packed_span;
    if (entry.is_compressed) {
        auto result = cricodecs::crilayla::decompress(*packed_span);
        if (!result) {
            return std::unexpected(
                "CPK extract failed: could not decompress CRILAYLA payload: " +
                result.error());
        }
        decompressed = std::move(*result);
        output_bytes = decompressed;
    }

    std::error_code error;
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            return std::unexpected(
                "CPK extract failed: could not create output directory: " +
                output_path.parent_path().string());
        }
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("CPK extract failed: could not open output: " + output_path.string());
    }

    if (auto result = writer.write(output_bytes); !result) {
        return std::unexpected("CPK extract failed: could not write output: " + output_path.string());
    }

    if (auto result = writer.close(); !result) {
        return std::unexpected("CPK extract failed: could not finalize output: " + output_path.string());
    }
    return {};
}

std::expected<std::vector<uint8_t>, std::string> Cpk::file_bytes(size_t index) const {
    if (index >= m_files.size()) {
        return std::unexpected("CPK file index out of range");
    }

    const auto& source = m_sources[index];
    const auto& entry = m_files[index];
    switch (source.kind) {
        case EntrySourceKind::Archive:
            return extract_to_memory(entry);
        case EntrySourceKind::FilePath: {
            io::reader reader;
            if (auto result = reader.open(source.path); !result) {
                return std::unexpected("CPK entry load failed: could not open input file: " + source.path.string());
            }
            const auto data = reader.data();
            return std::vector<uint8_t>(data.begin(), data.end());
        }
        case EntrySourceKind::OwnedBytes:
            return source.bytes;
    }
    return std::unexpected("CPK entry load failed: unsupported entry source");
}

std::expected<void, std::string> Cpk::extract(
    const CpkEntry& entry,
    const std::filesystem::path& output_dir
) const {
    const auto output_path = output_dir / entry.full_path();
    return write_entry_to_file(entry, output_path);
}

std::expected<void, std::string> Cpk::extract(
    const std::filesystem::path& output_dir,
    bool disambiguate_conflicts
) const {
    util::flat_unordered_set<std::string> used_paths;
    used_paths.reserve(m_files.size());

    for (const auto& entry : m_files) {
        const auto relative_path = disambiguate_conflicts
            ? disambiguate_output_path(entry, used_paths)
            : entry.full_path();
        const auto output_path = output_dir / relative_path;
        if (auto result = write_entry_to_file(entry, output_path); !result) {
            return result;
        }
    }

    return {};
}

CpkEntry* Cpk::try_file(size_t index) noexcept {
    return index < m_files.size() ? &m_files[index] : nullptr;
}

const CpkEntry* Cpk::try_file(size_t index) const noexcept {
    return index < m_files.size() ? &m_files[index] : nullptr;
}

CpkEntry& Cpk::edit_file(size_t index) {
    m_dirty = true;
    return m_files.at(index);
}

CpkOptions& Cpk::edit_options() noexcept {
    m_dirty = true;
    return m_options;
}

void Cpk::set_options(const CpkOptions& options) {
    m_options = options;
    m_dirty = true;
}

void Cpk::set_encoding(const text::EncodingOptions& encoding) {
    m_options.encoding = encoding;
}

std::expected<void, std::string> Cpk::parse() {
    m_cpk_header = utf::UtfTable{};
    m_toc = utf::UtfTable{};
    m_itoc = utf::UtfTable{};
    m_gtoc = utf::UtfTable{};
    m_etoc = utf::UtfTable{};

    m_has_declared_preset = false;
    m_declared_preset = CpkPreset::Custom;
    m_preset = CpkPreset::Custom;
    m_layout_mode = CpkMode::Mode1;
    m_content_offset = 0;
    m_align = 0x800;

    m_cpk_header_storage.clear();
    m_toc_storage.clear();
    m_itoc_storage.clear();
    m_gtoc_storage.clear();
    m_etoc_storage.clear();
    m_files.clear();
    m_sources.clear();

    if (!m_reader.is_open()) {
        return std::unexpected("CPK parse failed: no source is open");
    }
    if (m_reader.size() < chunk_header_size) {
        return std::unexpected("CPK source is too small");
    }

    auto cpk_header = load_chunk_utf(0, root_chunk_size, "CPK ");
    if (!cpk_header) {
        return std::unexpected(cpk_header.error());
    }
    m_cpk_header_storage = std::move(cpk_header->owned_payload);
    m_cpk_header = std::move(cpk_header->table);

    if (auto content_offset = get_optional_unsigned<uint64_t>(m_cpk_header, 0, "ContentOffset"); !content_offset) {
        return std::unexpected(content_offset.error());
    } else if (content_offset->has_value()) {
        m_content_offset = **content_offset;
    }

    if (auto align = get_optional_unsigned<uint16_t>(m_cpk_header, 0, "Align"); !align) {
        return std::unexpected(align.error());
    } else if (align->has_value() && **align != 0) {
        m_align = **align;
    }

    if (auto declared_preset = get_optional_unsigned<uint32_t>(m_cpk_header, 0, "CpkMode"); !declared_preset) {
        return std::unexpected(declared_preset.error());
    } else if (declared_preset->has_value()) {
        m_has_declared_preset = true;
        m_declared_preset = decode_declared_preset(**declared_preset);
    }

    auto maybe_load_chunk = [this](std::string_view offset_name,
                                   std::string_view size_name,
                                   std::string_view expected_magic,
                                   utf::UtfTable& destination,
                                   std::vector<uint8_t>& storage) -> std::expected<void, std::string> {
        auto offset = get_optional_unsigned<uint64_t>(m_cpk_header, 0, offset_name);
        if (!offset) {
            return std::unexpected(offset.error());
        }
        auto size = get_optional_unsigned<uint64_t>(m_cpk_header, 0, size_name);
        if (!size) {
            return std::unexpected(size.error());
        }

        const uint64_t chunk_offset = offset->value_or(0);
        const uint64_t chunk_size = size->value_or(0);
        if (chunk_offset == 0 || chunk_size == 0) {
            return {};
        }

        auto chunk = load_chunk_utf(chunk_offset, chunk_size, expected_magic);
        if (!chunk) {
            return std::unexpected(chunk.error());
        }
        storage = std::move(chunk->owned_payload);
        destination = std::move(chunk->table);
        return {};
    };

    if (auto result = maybe_load_chunk("TocOffset", "TocSize", "TOC ", m_toc, m_toc_storage); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = maybe_load_chunk("ItocOffset", "ItocSize", "ITOC", m_itoc, m_itoc_storage); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = maybe_load_chunk("GtocOffset", "GtocSize", "GTOC", m_gtoc, m_gtoc_storage); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = maybe_load_chunk("EtocOffset", "EtocSize", "ETOC", m_etoc, m_etoc_storage); !result) {
        return std::unexpected(result.error());
    }

    auto layout_mode = detect_layout_mode(has_toc(), has_itoc(), has_gtoc());
    if (!layout_mode) {
        return std::unexpected(layout_mode.error());
    }
    m_layout_mode = *layout_mode;
    m_preset = preset_from_chunks(has_toc(), has_itoc(), has_gtoc(), has_etoc());

    if (auto result = populate_file_entries(); !result) {
        return std::unexpected(result.error());
    }

    auto encoding = m_options.encoding;
    m_options = {};
    m_options.encoding = std::move(encoding);
    m_options.preset = m_has_declared_preset ? m_declared_preset : m_preset;
    m_options.enable_toc = has_toc();
    m_options.enable_itoc = has_itoc();
    m_options.enable_gtoc = has_gtoc();
    m_options.enable_etoc = has_etoc();
    m_options.align = m_align;

    auto enable_toc_crc = get_optional_unsigned<uint16_t>(m_cpk_header, 0, "EnableTocCrc");
    if (!enable_toc_crc) {
        return std::unexpected(enable_toc_crc.error());
    }
    auto enable_file_crc = get_optional_unsigned<uint16_t>(m_cpk_header, 0, "EnableFileCrc");
    if (!enable_file_crc) {
        return std::unexpected(enable_file_crc.error());
    }
    auto crc_mode = get_optional_unsigned<uint32_t>(m_cpk_header, 0, "CrcMode");
    if (!crc_mode) {
        return std::unexpected(crc_mode.error());
    }
    m_options.enable_crc = crc_mode->value_or(0) == 0 &&
        (enable_toc_crc->value_or(0) != 0 || enable_file_crc->value_or(0) != 0);

    auto tver = get_optional_string(m_cpk_header, 0, "Tvers");
    if (!tver) {
        return std::unexpected(tver.error());
    }
    m_options.tver = tver->empty() ? default_tver(m_options.preset) : *tver;

    auto comment = get_optional_string(m_cpk_header, 0, "Comment");
    if (!comment) {
        return std::unexpected(comment.error());
    }
    m_options.comment = comment->empty() ? std::string("<NULL>") : *comment;

    if (has_etoc() && m_etoc.row_count() > 0) {
        auto local_dir = get_optional_string(m_etoc, 0, "LocalDir");
        if (!local_dir) {
            return std::unexpected(local_dir.error());
        }
        m_options.etoc_local_dir = *local_dir;
    }

    m_dirty = false;
    return {};
}

std::expected<Cpk::LoadedUtfChunk, std::string> Cpk::load_chunk_utf(
    uint64_t offset,
    uint64_t declared_chunk_size,
    std::string_view expected_magic
) const {
    if (expected_magic.size() != 4) {
        return std::unexpected("CPK parse failed: expected chunk magic must be 4 bytes");
    }
    if (offset > m_reader.size() || m_reader.size() - offset < chunk_header_size) {
        return std::unexpected("CPK chunk header is out of range");
    }
    if (declared_chunk_size < chunk_header_size) {
        return std::unexpected("CPK chunk size is too small");
    }

    const auto header = m_reader.subspan(static_cast<size_t>(offset), 4);
    if (!std::equal(expected_magic.begin(), expected_magic.end(), header.begin())) {
        return std::unexpected("CPK parse failed: unexpected chunk magic while loading " + std::string(expected_magic));
    }

    const uint32_t enc_flag = m_reader.read_le_at<uint32_t>(static_cast<size_t>(offset + 0x04));
    const uint32_t utf_size = m_reader.read_le_at<uint32_t>(static_cast<size_t>(offset + 0x08));
    const uint64_t total_size = chunk_header_size + utf_size;

    if (total_size > declared_chunk_size) {
        return std::unexpected("CPK chunk payload exceeds the declared chunk size");
    }
    if (offset > m_reader.size() || total_size > m_reader.size() - offset) {
        return std::unexpected("CPK chunk payload is truncated");
    }

    const auto payload = m_reader.subspan(
        static_cast<size_t>(offset + chunk_header_size),
        utf_size
    );

    if (enc_flag != chunk_encrypted_flag) {
        LoadedUtfChunk chunk;
        chunk.owned_payload.assign(payload.begin(), payload.end());
        decrypt_utf_payload(chunk.owned_payload);

        auto table = utf::UtfTable::load(std::span<const uint8_t>(chunk.owned_payload));
        if (!table) {
            return std::unexpected(table.error());
        }
        chunk.table = std::move(*table);
        return chunk;
    }

    auto table = utf::UtfTable::load(payload);
    if (!table) {
        return std::unexpected(table.error());
    }
    LoadedUtfChunk chunk;
    chunk.table = std::move(*table);
    return chunk;
}

std::expected<void, std::string> Cpk::populate_file_entries() {
    m_files.clear();
    m_sources.clear();

    if (has_toc()) {
        const uint32_t toc_rows = m_toc.row_count();
        m_files.reserve(toc_rows);
        m_sources.reserve(toc_rows);

        const int dirname_column = m_toc.find_column("DirName");
        const int filename_column = m_toc.find_column("FileName");
        const int file_size_column = m_toc.find_column("FileSize");
        const int extract_size_column = m_toc.find_column("ExtractSize");
        const int file_offset_column = m_toc.find_column("FileOffset");
        const int id_column = m_toc.find_column("ID");
        const int user_string_column = m_toc.find_column("UserString");

        for (uint32_t row = 0; row < toc_rows; ++row) {
            CpkEntry entry;
            entry.toc_index = row;

            auto dirname = get_optional_string_at(m_toc, row, dirname_column, "DirName");
            if (!dirname) {
                return std::unexpected(dirname.error());
            }
            entry.dirname_raw = *dirname;
            if (!entry.dirname_raw.empty()) {
                auto decoded = decode_cri_string(entry.dirname_raw, m_options.encoding, "CPK DirName decode failed");
                if (!decoded) {
                    return std::unexpected(decoded.error());
                }
                entry.dirname = *decoded;
            }

            auto filename = get_optional_string_at(m_toc, row, filename_column, "FileName");
            if (!filename) {
                return std::unexpected(filename.error());
            }
            entry.filename_raw = *filename;
            if (!entry.filename_raw.empty()) {
                auto decoded = decode_cri_string(entry.filename_raw, m_options.encoding, "CPK FileName decode failed");
                if (!decoded) {
                    return std::unexpected(decoded.error());
                }
                entry.filename = *decoded;
            }

            auto packed_size = get_required_unsigned_at<uint64_t>(m_toc, row, file_size_column, "FileSize");
            if (!packed_size) {
                return std::unexpected(packed_size.error());
            }
            entry.file_size = *packed_size;

            auto extract_size = get_optional_unsigned_at<uint64_t>(m_toc, row, extract_size_column, "ExtractSize");
            if (!extract_size) {
                return std::unexpected(extract_size.error());
            }
            entry.extract_size = extract_size->value_or(entry.file_size);

            auto file_offset = get_required_unsigned_at<uint64_t>(m_toc, row, file_offset_column, "FileOffset");
            if (!file_offset) {
                return std::unexpected(file_offset.error());
            }
            entry.file_offset = root_chunk_size + *file_offset;

            auto id = get_optional_unsigned_at<uint32_t>(m_toc, row, id_column, "ID");
            if (!id) {
                return std::unexpected(id.error());
            }
            entry.id = id->value_or(row);

            auto user_string = get_optional_string_at(m_toc, row, user_string_column, "UserString");
            if (!user_string) {
                return std::unexpected(user_string.error());
            }
            if (!user_string->empty()) {
                entry.user_string = *user_string;
            }

            entry.is_compressed = entry.extract_size > entry.file_size;
            entry.request_compress = entry.is_compressed;
            EntrySource source;
            source.kind = EntrySourceKind::Archive;
            source.explicit_id = entry.id;
            m_files.push_back(std::move(entry));
            m_sources.push_back(std::move(source));
        }

        if (has_itoc() &&
            m_itoc.find_column("ID") >= 0 &&
            m_itoc.find_column("TocIndex") >= 0) {
            const int itoc_id_column = m_itoc.find_column("ID");
            const int toc_index_column = m_itoc.find_column("TocIndex");
            for (uint32_t row = 0; row < m_itoc.row_count(); ++row) {
                auto id = get_required_unsigned_at<uint32_t>(m_itoc, row, itoc_id_column, "ID");
                if (!id) {
                    return std::unexpected(id.error());
                }
                auto toc_index = get_required_unsigned_at<uint32_t>(m_itoc, row, toc_index_column, "TocIndex");
                if (!toc_index) {
                    return std::unexpected(toc_index.error());
                }
                if (*toc_index >= m_files.size()) {
                    return std::unexpected("CPK parse failed: ITOC TocIndex points outside the TOC row range");
                }
                m_files[*toc_index].id = *id;
            }
        }

        return {};
    }

    if (!has_itoc()) {
        return std::unexpected("CPK archive has no TOC or ITOC entries");
    }

    auto files_l = get_optional_unsigned<uint32_t>(m_itoc, 0, "FilesL");
    if (!files_l) {
        return std::unexpected(files_l.error());
    }
    auto files_h = get_optional_unsigned<uint32_t>(m_itoc, 0, "FilesH");
    if (!files_h) {
        return std::unexpected(files_h.error());
    }

    auto data_l_span = m_itoc.get_data(0, "DataL");
    if (!data_l_span) {
        return std::unexpected(data_l_span.error());
    }
    auto data_h_span = m_itoc.get_data(0, "DataH");
    if (!data_h_span) {
        return std::unexpected(data_h_span.error());
    }

    auto data_l = load_nested_utf(*data_l_span);
    if (!data_l) {
        return std::unexpected(data_l.error());
    }
    auto data_h = load_nested_utf(*data_h_span);
    if (!data_h) {
        return std::unexpected(data_h.error());
    }

    auto add_mode0_entries = [this](const utf::UtfTable& table,
                                    uint32_t row_limit) -> std::expected<void, std::string> {
        const uint32_t safe_row_limit = std::min<uint32_t>(row_limit, table.row_count());
        const int id_column = table.find_column("ID");
        const int file_size_column = table.find_column("FileSize");
        const int extract_size_column = table.find_column("ExtractSize");
        for (uint32_t row = 0; row < safe_row_limit; ++row) {
            CpkEntry entry;
            auto id = get_required_unsigned_at<uint32_t>(table, row, id_column, "ID");
            if (!id) {
                return std::unexpected(id.error());
            }
            entry.id = *id;

            auto file_size = get_required_unsigned_at<uint64_t>(table, row, file_size_column, "FileSize");
            if (!file_size) {
                return std::unexpected(file_size.error());
            }
            entry.file_size = *file_size;

            auto extract_size = get_optional_unsigned_at<uint64_t>(table, row, extract_size_column, "ExtractSize");
            if (!extract_size) {
                return std::unexpected(extract_size.error());
            }
            entry.extract_size = extract_size->value_or(entry.file_size);
            entry.is_compressed = entry.extract_size > entry.file_size;
            entry.request_compress = entry.is_compressed;

            EntrySource source;
            source.kind = EntrySourceKind::Archive;
            source.explicit_id = entry.id;
            m_files.push_back(std::move(entry));
            m_sources.push_back(std::move(source));
        }

        return {};
    };

    m_files.reserve(files_l->value_or(0) + files_h->value_or(0));
    m_sources.reserve(files_l->value_or(0) + files_h->value_or(0));
    if (auto result = add_mode0_entries(*data_l, files_l->value_or(0)); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = add_mode0_entries(*data_h, files_h->value_or(0)); !result) {
        return std::unexpected(result.error());
    }

    auto order = stable_mode0_id_order(m_files);

    std::vector<CpkEntry> sorted_files;
    std::vector<EntrySource> sorted_sources;
    sorted_files.reserve(m_files.size());
    sorted_sources.reserve(m_sources.size());
    for (const size_t index : order) {
        sorted_files.push_back(std::move(m_files[index]));
        sorted_sources.push_back(std::move(m_sources[index]));
    }
    m_files = std::move(sorted_files);
    m_sources = std::move(sorted_sources);

    uint64_t running_offset = m_content_offset;
    for (size_t index = 0; index < m_files.size(); ++index) {
        auto& entry = m_files[index];
        entry.toc_index = static_cast<uint32_t>(index);
        entry.file_offset = running_offset;
        running_offset += align_up(entry.file_size, m_align);
    }

    return {};
}

void Cpk::normalize_entry_path(CpkEntry& entry, const std::string& cpk_path) const {
    const auto normalized = std::filesystem::path(cpk_path).lexically_normal();
    entry.filename = normalized.filename().generic_string();
    entry.filename_raw.clear();
    entry.dirname.clear();
    entry.dirname_raw.clear();
    if (normalized.has_parent_path()) {
        const auto dirname = normalized.parent_path().generic_string();
        if (dirname != ".") {
            entry.dirname = dirname;
        }
    }
}

std::expected<void, std::string> CpkReader::load(const std::filesystem::path& path) {
    return m_cpk.load_from_path(path);
}

std::expected<void, std::string> CpkReader::load(std::span<const uint8_t> data) {
    return m_cpk.load_from_bytes(data);
}

std::expected<std::vector<uint8_t>, std::string> CpkReader::extract_to_memory(const CpkEntry& entry) const {
    return m_cpk.extract_to_memory(entry);
}

std::expected<void, std::string> CpkReader::extract(
    const CpkEntry& entry,
    const std::filesystem::path& output_dir
) const {
    return m_cpk.extract(entry, output_dir);
}

std::expected<void, std::string> CpkReader::extract(
    const std::filesystem::path& output_dir,
    bool disambiguate_conflicts
) const {
    return m_cpk.extract(output_dir, disambiguate_conflicts);
}

} // namespace cricodecs::cpk
