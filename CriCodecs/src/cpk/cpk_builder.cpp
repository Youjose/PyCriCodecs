/**
 * @file cpk_builder.cpp
 * @brief CPK save/build path for the unified mutable CPK object.
 */

#include "cpk_container.hpp"
#include "crilayla.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <unordered_set>
#include <vector>

#include "../utilities/numeric.hpp"
#include "../utilities/crc.hpp"
#include "../utilities/text_encoding.hpp"

namespace cricodecs::cpk {

namespace {

using util::align_up;

constexpr uint32_t toc_crc_seed = 0xDEADBEEFu;
constexpr uint32_t itoc_crc_seed = 0xBEEFCAEDu;
constexpr uint32_t gtoc_crc_seed = 0x00BEEF80u;

struct PresetFlags {
    bool toc = false;
    bool itoc = false;
    bool gtoc = false;
    bool etoc = false;
};

PresetFlags flags_for_preset(CpkPreset preset) {
    switch (preset) {
        case CpkPreset::Id:
            return {false, true, false, false};
        case CpkPreset::Filename:
            return {true, false, false, true};
        case CpkPreset::FilenameId:
            return {true, true, false, true};
        case CpkPreset::FilenameGroup:
            return {true, false, true, true};
        case CpkPreset::IdGroup:
            return {true, true, true, false};
        case CpkPreset::FilenameIdGroup:
            return {true, true, true, true};
        case CpkPreset::Custom:
        default:
            return {};
    }
}

PresetFlags enabled_chunks(const CpkOptions& options) {
    auto flags = flags_for_preset(options.preset);
    flags.toc = options.enable_toc.value_or(flags.toc);
    flags.itoc = options.enable_itoc.value_or(flags.itoc);
    flags.gtoc = options.enable_gtoc.value_or(flags.gtoc);
    flags.etoc = options.enable_etoc.value_or(flags.etoc);
    return flags;
}

uint32_t header_mode_value(CpkPreset preset) {
    if (preset == CpkPreset::Custom) {
        return 0xFFFFFFFFu;
    }
    return static_cast<uint32_t>(preset);
}

std::vector<size_t> sorted_indices(size_t count, auto&& comparator) {
    std::vector<size_t> indices(count);
    for (size_t index = 0; index < count; ++index) {
        indices[index] = index;
    }
    std::stable_sort(indices.begin(), indices.end(), comparator);
    return indices;
}

std::expected<std::string, std::string> raw_or_encoded(
    std::string_view raw,
    std::string_view text_value,
    const text::EncodingOptions& encoding,
    std::string_view context
) {
    if (!raw.empty()) {
        return std::string(raw);
    }
    if (text_value.empty()) {
        return std::string{};
    }
    auto encoded = text::encode_cri_string(text_value, encoding);
    if (!encoded) {
        return std::unexpected(std::string(context) + ": " + encoded.error());
    }
    return std::string(reinterpret_cast<const char*>(encoded->data()), encoded->size());
}

uint32_t cpk_crc32(std::span<const uint8_t> data, uint32_t seed) noexcept {
    return util::CriCrc32::checksum(data, seed);
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> Cpk::save() {
    if (!m_dirty && m_reader.is_open()) {
        const auto data = m_reader.data();
        return std::vector<uint8_t>(data.begin(), data.end());
    }
    return save_impl(false);
}

std::expected<std::vector<uint8_t>, std::string> Cpk::decrypt() {
    return save_impl(false);
}

std::expected<std::vector<uint8_t>, std::string> Cpk::encrypt() {
    return save_impl(true);
}

std::expected<std::vector<uint8_t>, std::string> Cpk::save_impl(bool encrypt_utf_chunks) {
    if (auto result = rebuild_state(encrypt_utf_chunks); !result) {
        return std::unexpected(result.error());
    }
    return m_owned_archive_bytes;
}

std::expected<void, std::string> Cpk::rebuild_state(bool encrypt_utf_chunks) {
    if (m_files.empty()) {
        return std::unexpected("CPK build failed: archive has no entries");
    }
    if (m_options.align == 0) {
        return std::unexpected("CPK alignment must be non-zero");
    }

    auto prepared_entries = prepare_entries_for_save();
    if (!prepared_entries) {
        return std::unexpected(prepared_entries.error());
    }

    auto archive = build_archive(*prepared_entries, encrypt_utf_chunks);
    if (!archive) {
        return std::unexpected(archive.error());
    }

    m_owned_archive_bytes = std::move(*archive);
    m_source_path.clear();
    m_reader = io::reader{};
    if (auto result = m_reader.open(std::span<const uint8_t>(m_owned_archive_bytes)); !result) {
        return std::unexpected(std::string(result.error()));
    }
    if (auto result = parse(); !result) {
        return std::unexpected(result.error());
    }

    return {};
}

std::expected<void, std::string> Cpk::save_to_file(const std::filesystem::path& output_path) {
    if (m_dirty || !m_reader.is_open()) {
        if (auto result = rebuild_state(false); !result) {
            return std::unexpected(result.error());
        }
    }

    if (auto result = io::write_file_bytes(output_path, m_reader.data(), "CPK build failed"); !result) {
        return result;
    }

    m_source_path = output_path;
    return {};
}

std::expected<std::vector<Cpk::PreparedEntry>, std::string> Cpk::prepare_entries_for_save() {
    const auto chunks = enabled_chunks(m_options);

    if (!chunks.toc && !chunks.itoc) {
        return std::unexpected("CPK build failed: archive must emit at least a TOC or an ITOC");
    }

    std::unordered_set<uint32_t> used_ids;
    for (size_t index = 0; index < m_files.size(); ++index) {
        const auto& source = m_sources[index];
        if (source.explicit_id.has_value()) {
            if (!used_ids.insert(*source.explicit_id).second) {
                return std::unexpected("CPK builder does not support duplicate file IDs");
            }
        }
    }

    uint32_t next_auto_id = 0;
    std::vector<PreparedEntry> prepared_entries;
    prepared_entries.reserve(m_files.size());

    for (size_t index = 0; index < m_files.size(); ++index) {
        auto& entry = m_files[index];
        const auto& source = m_sources[index];

        if (chunks.toc && entry.filename.empty()) {
            return std::unexpected("CPK entries must have a filename when TOC output is enabled");
        }

        PreparedEntry prepared;

        if (source.explicit_id.has_value()) {
            prepared.effective_id = *source.explicit_id;
        } else {
            while (used_ids.contains(next_auto_id)) {
                ++next_auto_id;
            }
            prepared.effective_id = next_auto_id;
            used_ids.insert(next_auto_id);
            ++next_auto_id;
        }

        if (!chunks.toc && entry.filename.empty()) {
            entry.dirname.clear();
        }

        if (chunks.itoc && !chunks.toc && prepared.effective_id > std::numeric_limits<uint16_t>::max()) {
            return std::unexpected("CPK build failed: mode-0 style ITOC archives require 16-bit file IDs");
        }

        const bool can_preserve_packed =
            std::holds_alternative<ArchiveSource>(source.data) &&
            entry.request_compress == entry.is_compressed;

        if (can_preserve_packed) {
            auto packed_span = packed_entry_span(entry);
            if (!packed_span) {
                return std::unexpected(packed_span.error());
            }
            prepared.payload = *packed_span;
            prepared.unpacked_size = entry.extract_size;
            prepared_entries.push_back(std::move(prepared));
            continue;
        }

        auto raw_bytes = file_bytes(index);
        if (!raw_bytes) {
            return std::unexpected(raw_bytes.error());
        }
        if (raw_bytes->size() > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected("CPK builder currently supports files up to 4 GiB");
        }

        prepared.unpacked_size = raw_bytes->size();

        if (entry.request_compress) {
            auto compressed = cricodecs::crilayla::compress(*raw_bytes);
            if (compressed.size() < raw_bytes->size()) {
                prepared.owned_payload = std::move(compressed);
                prepared.payload = std::span<const uint8_t>(prepared.owned_payload.data(), prepared.owned_payload.size());
            } else {
                prepared.owned_payload = std::move(*raw_bytes);
                prepared.payload = std::span<const uint8_t>(prepared.owned_payload.data(), prepared.owned_payload.size());
            }
        } else {
            prepared.owned_payload = std::move(*raw_bytes);
            prepared.payload = std::span<const uint8_t>(prepared.owned_payload.data(), prepared.owned_payload.size());
        }

        prepared.crc32 = cpk_crc32(prepared.payload, 0);
        prepared_entries.push_back(std::move(prepared));
    }

    return prepared_entries;
}

std::expected<std::vector<uint8_t>, std::string> Cpk::build_archive(
    std::vector<PreparedEntry>& prepared_entries,
    bool encrypt_utf_chunks
) {
    const auto chunks = enabled_chunks(m_options);

    if (!chunks.toc && !chunks.itoc) {
        return std::unexpected("CPK build failed: archive must emit at least a TOC or an ITOC");
    }
    if (chunks.gtoc && !chunks.toc) {
        return std::unexpected("CPK build failed: GTOC requires TOC support in the current builder");
    }

    if (m_options.tver.empty()) {
        m_options.tver = default_tool_version;
    }

    uint64_t enabled_packed_size = 0;
    uint64_t enabled_data_size = 0;
    uint64_t content_size = 0;
    for (const auto& prepared : prepared_entries) {
        enabled_packed_size += prepared.payload.size();
        enabled_data_size += prepared.unpacked_size;
        content_size += align_up(static_cast<uint64_t>(prepared.payload.size()), m_options.align);
    }

    std::vector<size_t> toc_order;
    if (chunks.toc) {
        std::vector<std::string> toc_sort_keys;
        toc_sort_keys.reserve(prepared_entries.size());
        for (size_t index = 0; index < prepared_entries.size(); ++index) {
            toc_sort_keys.push_back(m_files[index].full_path().generic_string());
        }
        toc_order = sorted_indices(prepared_entries.size(), [&toc_sort_keys](size_t lhs, size_t rhs) {
            return compare_archive_paths(toc_sort_keys[lhs], toc_sort_keys[rhs]) < 0;
        });
    }

    std::vector<size_t> generated_data_order;
    if (chunks.itoc) {
        generated_data_order = sorted_indices(prepared_entries.size(), [&prepared_entries](size_t lhs, size_t rhs) {
            if (prepared_entries[lhs].effective_id != prepared_entries[rhs].effective_id) {
                return prepared_entries[lhs].effective_id < prepared_entries[rhs].effective_id;
            }
            return lhs < rhs;
        });
    }
    const std::vector<size_t>& data_order = chunks.itoc ? generated_data_order : toc_order;

    auto wrap_aligned = [&](std::string_view magic, std::span<const uint8_t> payload) {
        auto chunk = wrap_chunk(magic, payload, encrypt_utf_chunks);
        chunk.resize(static_cast<size_t>(align_up(chunk.size(), chunk_alignment)), 0);
        return chunk;
    };

    std::vector<uint8_t> toc_chunk;
    std::vector<uint8_t> toc_payload;
    if (chunks.toc) {
        const std::vector<uint64_t> placeholder_offsets(prepared_entries.size(), 0);
        auto toc_data = generate_toc(prepared_entries, toc_order, placeholder_offsets);
        if (!toc_data) {
            return std::unexpected(toc_data.error());
        }
        toc_payload = std::move(*toc_data);
        toc_chunk = wrap_aligned("TOC ", toc_payload);
    }

    std::vector<uint8_t> itoc_chunk;
    std::vector<uint8_t> itoc_payload;
    if (chunks.itoc) {
        itoc_payload = chunks.toc
            ? generate_itoc_mode2(prepared_entries, toc_order)
            : generate_itoc_mode0(prepared_entries, data_order);
        itoc_chunk = wrap_aligned("ITOC", itoc_payload);
    }

    std::vector<uint8_t> gtoc_chunk;
    std::vector<uint8_t> gtoc_payload;
    if (chunks.gtoc) {
        gtoc_payload = generate_gtoc(prepared_entries, enabled_packed_size);
        gtoc_chunk = wrap_aligned("GTOC", gtoc_payload);
    }

    std::vector<uint8_t> etoc_chunk;
    if (chunks.etoc) {
        auto etoc_data = generate_etoc();
        if (!etoc_data) {
            return std::unexpected(etoc_data.error());
        }
        etoc_chunk = wrap_aligned("ETOC", *etoc_data);
    }

    std::vector<uint64_t> entry_offsets(prepared_entries.size(), 0);
    auto assign_entry_offsets = [&](uint64_t content_offset) {
        uint64_t offset = content_offset - root_chunk_size;
        for (const size_t index : data_order) {
            entry_offsets[index] = offset;
            offset += align_up(
                static_cast<uint64_t>(prepared_entries[index].payload.size()),
                m_options.align
            );
        }
    };
    if (chunks.toc) {
        for (size_t pass = 0; pass < 4; ++pass) {
            const uint64_t content_offset = root_chunk_size + toc_chunk.size() + itoc_chunk.size() + gtoc_chunk.size();
            assign_entry_offsets(content_offset);

            auto toc_data = generate_toc(prepared_entries, toc_order, entry_offsets);
            if (!toc_data) {
                return std::unexpected(toc_data.error());
            }
            auto updated_toc_payload = std::move(*toc_data);
            auto updated_toc_chunk = wrap_aligned("TOC ", updated_toc_payload);
            if (updated_toc_chunk.size() == toc_chunk.size()) {
                toc_payload = std::move(updated_toc_payload);
                toc_chunk = std::move(updated_toc_chunk);
                break;
            }
            toc_payload = std::move(updated_toc_payload);
            toc_chunk = std::move(updated_toc_chunk);
        }
    }

    const uint64_t toc_chunk_offset = root_chunk_size;
    const uint64_t itoc_chunk_offset = chunks.toc
        ? toc_chunk_offset + toc_chunk.size()
        : root_chunk_size;
    const uint64_t gtoc_chunk_offset = chunks.toc
        ? toc_chunk_offset + toc_chunk.size() + itoc_chunk.size()
        : root_chunk_size + itoc_chunk.size();
    const uint64_t content_offset = root_chunk_size + toc_chunk.size() + itoc_chunk.size() + gtoc_chunk.size();

    if (chunks.toc) {
        assign_entry_offsets(content_offset);
        auto toc_data = generate_toc(prepared_entries, toc_order, entry_offsets);
        if (!toc_data) {
            return std::unexpected(toc_data.error());
        }
        toc_payload = std::move(*toc_data);
        toc_chunk = wrap_aligned("TOC ", toc_payload);
    }

    const uint64_t etoc_chunk_offset = chunks.etoc ? content_offset + content_size : 0;
    const uint64_t file_size = content_offset + content_size + etoc_chunk.size();

    const uint32_t toc_crc = m_options.enable_crc && chunks.toc
        ? cpk_crc32(toc_payload, toc_crc_seed)
        : 0u;
    const uint32_t itoc_crc = m_options.enable_crc && chunks.itoc
        ? cpk_crc32(itoc_payload, itoc_crc_seed)
        : 0u;
    const uint32_t gtoc_crc = m_options.enable_crc && chunks.gtoc
        ? cpk_crc32(gtoc_payload, gtoc_crc_seed)
        : 0u;

    auto cpk_utf = generate_cpk_header(
        enabled_packed_size,
        enabled_data_size,
        content_size,
        toc_chunk.size(),
        chunks.itoc ? itoc_chunk_offset : 0,
        itoc_chunk.size(),
        etoc_chunk_offset,
        etoc_chunk.size(),
        chunks.gtoc ? gtoc_chunk_offset : 0,
        gtoc_chunk.size(),
        content_offset,
        file_size,
        toc_crc,
        itoc_crc,
        gtoc_crc
    );
    auto cpk_chunk = wrap_chunk("CPK ", cpk_utf, encrypt_utf_chunks);
    cpk_chunk.resize(static_cast<size_t>(align_up(cpk_chunk.size(), chunk_alignment)), 0);
    if (cpk_chunk.size() != root_chunk_size) {
        return std::unexpected("CPK build failed: generated header chunk has an unexpected size");
    }

    static constexpr std::array<uint8_t, 6> cri_signature = {'(', 'c', ')', 'C', 'R', 'I'};
    std::copy(cri_signature.begin(), cri_signature.end(), cpk_chunk.end() - cri_signature.size());

    if (file_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return std::unexpected("CPK archive is too large to materialize in memory");
    }

    std::vector<uint8_t> archive;
    archive.reserve(static_cast<size_t>(file_size));
    archive.insert(archive.end(), cpk_chunk.begin(), cpk_chunk.end());
    archive.insert(archive.end(), toc_chunk.begin(), toc_chunk.end());
    archive.insert(archive.end(), itoc_chunk.begin(), itoc_chunk.end());
    archive.insert(archive.end(), gtoc_chunk.begin(), gtoc_chunk.end());

    for (const size_t index : data_order) {
        const auto& prepared = prepared_entries[index];
        archive.insert(archive.end(), prepared.payload.begin(), prepared.payload.end());
        archive.resize(static_cast<size_t>(align_up(archive.size(), m_options.align)), 0);
    }

    archive.insert(archive.end(), etoc_chunk.begin(), etoc_chunk.end());
    if (archive.size() != file_size) {
        return std::unexpected("CPK build failed: generated size did not match the computed header size");
    }

    return archive;
}

std::expected<std::vector<uint8_t>, std::string> Cpk::generate_toc(
    const std::vector<PreparedEntry>& prepared_entries,
    const std::vector<size_t>& toc_order,
    const std::vector<uint64_t>& entry_offsets
) const {
    utf::UtfTable table = utf::UtfTable::create("CpkTocInfo");
    table.add_column("DirName", utf::ColumnType::String);
    table.add_column("FileName", utf::ColumnType::String);
    table.add_column("FileSize", utf::ColumnType::UInt32);
    table.add_column("ExtractSize", utf::ColumnType::UInt32);
    table.add_column("FileOffset", utf::ColumnType::UInt64);
    table.add_column("ID", utf::ColumnType::UInt32);
    table.add_column("UserString", utf::ColumnType::String);
    if (m_options.enable_crc) {
        table.add_column("CRC", utf::ColumnType::UInt32);
    }

    for (const size_t index : toc_order) {
        const auto row = table.add_row();
        const auto& entry = m_files[index];
        const auto& prepared = prepared_entries[index];

        auto dirname = raw_or_encoded(entry.dirname_raw, entry.dirname, m_options.encoding, "CPK DirName encode failed");
        if (!dirname) {
            return std::unexpected(dirname.error());
        }
        auto filename = raw_or_encoded(entry.filename_raw, entry.filename, m_options.encoding, "CPK FileName encode failed");
        if (!filename) {
            return std::unexpected(filename.error());
        }

        table.set(row, "DirName", *dirname).value();
        table.set(row, "FileName", *filename).value();
        table.set(row, "FileSize", static_cast<uint32_t>(prepared.payload.size())).value();
        table.set(row, "ExtractSize", static_cast<uint32_t>(prepared.unpacked_size)).value();
        table.set(row, "FileOffset", entry_offsets[index]).value();
        table.set(row, "ID", prepared.effective_id).value();
        table.set(row, "UserString", entry.user_string).value();
        if (m_options.enable_crc) {
            table.set(row, "CRC", prepared.crc32).value();
        }
    }

    return table.build();
}

std::vector<uint8_t> Cpk::generate_itoc_mode0(
    const std::vector<PreparedEntry>& prepared_entries,
    const std::vector<size_t>& data_order
) const {
    utf::UtfTable data_l = utf::UtfTable::create("CpkItocL");
    data_l.add_column("ID", utf::ColumnType::UInt16);
    data_l.add_column("FileSize", utf::ColumnType::UInt16);
    data_l.add_column("ExtractSize", utf::ColumnType::UInt16);
    if (m_options.enable_crc) {
        data_l.add_column("CRC", utf::ColumnType::UInt32);
    }

    utf::UtfTable data_h = utf::UtfTable::create("CpkItocH");
    data_h.add_column("ID", utf::ColumnType::UInt16);
    data_h.add_column("FileSize", utf::ColumnType::UInt32);
    data_h.add_column("ExtractSize", utf::ColumnType::UInt32);
    if (m_options.enable_crc) {
        data_h.add_column("CRC", utf::ColumnType::UInt32);
    }

    uint32_t files_l = 0;
    uint32_t files_h = 0;
    for (const size_t index : data_order) {
        const auto& prepared = prepared_entries[index];
        const bool fits_l = prepared.payload.size() <= std::numeric_limits<uint16_t>::max() &&
            prepared.unpacked_size <= std::numeric_limits<uint16_t>::max();

        if (fits_l) {
            const auto row = data_l.add_row();
            data_l.set(row, "ID", static_cast<uint16_t>(prepared.effective_id)).value();
            data_l.set(row, "FileSize", static_cast<uint16_t>(prepared.payload.size())).value();
            data_l.set(row, "ExtractSize", static_cast<uint16_t>(prepared.unpacked_size)).value();
            if (m_options.enable_crc) {
                data_l.set(row, "CRC", prepared.crc32).value();
            }
            ++files_l;
        } else {
            const auto row = data_h.add_row();
            data_h.set(row, "ID", static_cast<uint16_t>(prepared.effective_id)).value();
            data_h.set(row, "FileSize", static_cast<uint32_t>(prepared.payload.size())).value();
            data_h.set(row, "ExtractSize", static_cast<uint32_t>(prepared.unpacked_size)).value();
            if (m_options.enable_crc) {
                data_h.set(row, "CRC", prepared.crc32).value();
            }
            ++files_h;
        }
    }

    if (files_l == 0) {
        const auto row = data_l.add_row();
        data_l.set(row, "ID", static_cast<uint16_t>(0)).value();
        data_l.set(row, "FileSize", static_cast<uint16_t>(0)).value();
        data_l.set(row, "ExtractSize", static_cast<uint16_t>(0)).value();
        if (m_options.enable_crc) {
            data_l.set(row, "CRC", 0u).value();
        }
    }
    if (files_h == 0) {
        const auto row = data_h.add_row();
        data_h.set(row, "ID", static_cast<uint16_t>(0)).value();
        data_h.set(row, "FileSize", static_cast<uint32_t>(0)).value();
        data_h.set(row, "ExtractSize", static_cast<uint32_t>(0)).value();
        if (m_options.enable_crc) {
            data_h.set(row, "CRC", 0u).value();
        }
    }

    utf::UtfTable table = utf::UtfTable::create("CpkItocInfo");
    table.add_column("FilesL", utf::ColumnType::UInt32);
    table.add_column("FilesH", utf::ColumnType::UInt32);
    table.add_column("DataL", utf::ColumnType::VLData);
    table.add_column("DataH", utf::ColumnType::VLData);

    const auto row = table.add_row();
    table.set(row, "FilesL", files_l).value();
    table.set(row, "FilesH", files_h).value();
    table.set(row, "DataL", data_l.build()).value();
    table.set(row, "DataH", data_h.build()).value();
    return table.build();
}

std::vector<uint8_t> Cpk::generate_itoc_mode2(
    const std::vector<PreparedEntry>& prepared_entries,
    const std::vector<size_t>& toc_order
) const {
    std::vector<uint32_t> toc_index_by_entry(m_files.size(), 0);
    for (uint32_t toc_index = 0; toc_index < toc_order.size(); ++toc_index) {
        toc_index_by_entry[toc_order[toc_index]] = toc_index;
    }

    const auto data_order = sorted_indices(m_files.size(), [&prepared_entries](size_t lhs, size_t rhs) {
        if (prepared_entries[lhs].effective_id != prepared_entries[rhs].effective_id) {
            return prepared_entries[lhs].effective_id < prepared_entries[rhs].effective_id;
        }
        return lhs < rhs;
    });

    utf::UtfTable table = utf::UtfTable::create("CpkExtendId");
    table.add_column("ID", utf::ColumnType::UInt32);
    table.add_column("TocIndex", utf::ColumnType::UInt32);

    for (const size_t index : data_order) {
        const auto row = table.add_row();
        table.set(row, "ID", prepared_entries[index].effective_id).value();
        table.set(row, "TocIndex", toc_index_by_entry[index]).value();
    }

    return table.build();
}

std::expected<std::vector<uint8_t>, std::string> Cpk::generate_etoc() const {
    utf::UtfTable table = utf::UtfTable::create("CpkEtocInfo");
    table.add_column("UpdateDateTime", utf::ColumnType::UInt64);
    table.add_column("LocalDir", utf::ColumnType::String);

    const auto row = table.add_row();
    table.set(row, "UpdateDateTime", 0ull).value();
    table.set(row, "LocalDir", m_options.etoc_local_dir).value();
    return table.build();
}

std::vector<uint8_t> Cpk::generate_gtoc(
    const std::vector<PreparedEntry>& prepared_entries,
    uint64_t enabled_packed_size
) const {
    utf::UtfTable gdata = utf::UtfTable::create("CpkGtocGlink");
    gdata.add_column("Gname", utf::ColumnType::String);
    gdata.add_column("Child", utf::ColumnType::SInt32);
    gdata.add_column("Next", utf::ColumnType::SInt32);

    {
        const auto row = gdata.add_row();
        gdata.set(row, "Gname", std::string("")).value();
        gdata.set(row, "Child", static_cast<int32_t>(-1)).value();
        gdata.set(row, "Next", static_cast<int32_t>(0)).value();
    }
    {
        const auto row = gdata.add_row();
        gdata.set(row, "Gname", std::string("(none)")).value();
        gdata.set(row, "Child", static_cast<int32_t>(0)).value();
        gdata.set(row, "Next", static_cast<int32_t>(0)).value();
    }

    utf::UtfTable fdata = utf::UtfTable::create("CpkGtocFlink");
    fdata.add_column("Next", utf::ColumnType::SInt32);
    fdata.add_column("Child", utf::ColumnType::SInt32);
    fdata.add_column("SortFlink", utf::ColumnType::SInt32);
    fdata.add_column("Aindex", utf::ColumnType::UInt16);

    {
        const auto row = fdata.add_row();
        fdata.set(row, "Next", static_cast<int32_t>(-1)).value();
        fdata.set(row, "Child", static_cast<int32_t>(-1)).value();
        fdata.set(row, "SortFlink", static_cast<int32_t>(2)).value();
        fdata.set(row, "Aindex", static_cast<uint16_t>(0)).value();
    }
    {
        const auto row = fdata.add_row();
        fdata.set(row, "Next", static_cast<int32_t>(2)).value();
        fdata.set(row, "Child", static_cast<int32_t>(0)).value();
        fdata.set(row, "SortFlink", static_cast<int32_t>(1)).value();
        fdata.set(row, "Aindex", static_cast<uint16_t>(0)).value();
    }
    {
        const auto row = fdata.add_row();
        fdata.set(row, "Next", static_cast<int32_t>(0)).value();
        fdata.set(row, "Child", static_cast<int32_t>(1)).value();
        fdata.set(row, "SortFlink", static_cast<int32_t>(2)).value();
        fdata.set(row, "Aindex", static_cast<uint16_t>(0)).value();
    }

    utf::UtfTable attr_data = utf::UtfTable::create("CpkGtocAttr");
    attr_data.add_column("Aname", utf::ColumnType::String);
    attr_data.add_column("Align", utf::ColumnType::UInt16);
    attr_data.add_column("Files", utf::ColumnType::UInt32);
    attr_data.add_column("FileSize", utf::ColumnType::UInt32);

    {
        const auto row = attr_data.add_row();
        attr_data.set(row, "Aname", std::string("")).value();
        attr_data.set(row, "Align", m_options.align).value();
        attr_data.set(row, "Files", static_cast<uint32_t>(prepared_entries.size())).value();
        attr_data.set(row, "FileSize", static_cast<uint32_t>(enabled_packed_size)).value();
    }

    utf::UtfTable table = utf::UtfTable::create("CpkGtocInfo");
    table.add_column("Glink", utf::ColumnType::UInt32);
    table.add_column("Flink", utf::ColumnType::UInt32);
    table.add_column("Attr", utf::ColumnType::UInt32);
    table.add_column("Gdata", utf::ColumnType::VLData);
    table.add_column("Fdata", utf::ColumnType::VLData);
    table.add_column("AttrData", utf::ColumnType::VLData);

    const auto row = table.add_row();
    table.set(row, "Glink", static_cast<uint32_t>(2)).value();
    table.set(row, "Flink", static_cast<uint32_t>(3)).value();
    table.set(row, "Attr", static_cast<uint32_t>(1)).value();
    table.set(row, "Gdata", gdata.build()).value();
    table.set(row, "Fdata", fdata.build()).value();
    table.set(row, "AttrData", attr_data.build()).value();
    return table.build();
}

std::vector<uint8_t> Cpk::generate_cpk_header(
    uint64_t enabled_packed_size,
    uint64_t enabled_data_size,
    uint64_t content_size,
    uint64_t toc_chunk_size,
    uint64_t itoc_chunk_offset,
    uint64_t itoc_chunk_size,
    uint64_t etoc_chunk_offset,
    uint64_t etoc_chunk_size,
    uint64_t gtoc_chunk_offset,
    uint64_t gtoc_chunk_size,
    uint64_t content_offset,
    uint64_t file_size,
    uint32_t toc_crc,
    uint32_t itoc_crc,
    uint32_t gtoc_crc
) const {
    const bool has_toc = toc_chunk_size != 0;
    const bool has_itoc = itoc_chunk_size != 0;
    const bool has_gtoc = gtoc_chunk_size != 0;
    std::unordered_set<std::string_view> directories;
    for (const auto& entry : m_files) {
        if (has_toc && !entry.dirname.empty()) {
            directories.insert(entry.dirname);
        }
    }

    utf::UtfTable table = utf::UtfTable::create("CpkHeader");
    table.add_column("UpdateDateTime", utf::ColumnType::UInt64);
    table.add_column("FileSize", utf::ColumnType::UInt64);
    table.add_column("ContentOffset", utf::ColumnType::UInt64);
    table.add_column("ContentSize", utf::ColumnType::UInt64);
    table.add_column("TocOffset", utf::ColumnType::UInt64);
    table.add_column("TocSize", utf::ColumnType::UInt64);
    table.add_column("TocCrc", utf::ColumnType::UInt32);
    table.add_column("HtocOffset", utf::ColumnType::UInt64);
    table.add_column("HtocSize", utf::ColumnType::UInt64);
    table.add_column("EtocOffset", utf::ColumnType::UInt64);
    table.add_column("EtocSize", utf::ColumnType::UInt64);
    table.add_column("ItocOffset", utf::ColumnType::UInt64);
    table.add_column("ItocSize", utf::ColumnType::UInt64);
    table.add_column("ItocCrc", utf::ColumnType::UInt32);
    table.add_column("GtocOffset", utf::ColumnType::UInt64);
    table.add_column("GtocSize", utf::ColumnType::UInt64);
    table.add_column("GtocCrc", utf::ColumnType::UInt32);
    table.add_column("HgtocOffset", utf::ColumnType::UInt64);
    table.add_column("HgtocSize", utf::ColumnType::UInt64);
    table.add_column("EnabledPackedSize", utf::ColumnType::UInt64);
    table.add_column("EnabledDataSize", utf::ColumnType::UInt64);
    table.add_column("TotalDataSize", utf::ColumnType::UInt64);
    table.add_column("Tocs", utf::ColumnType::UInt32);
    table.add_column("Files", utf::ColumnType::UInt32);
    table.add_column("Groups", utf::ColumnType::UInt32);
    table.add_column("Attrs", utf::ColumnType::UInt32);
    table.add_column("TotalFiles", utf::ColumnType::UInt32);
    table.add_column("Directories", utf::ColumnType::UInt32);
    table.add_column("Updates", utf::ColumnType::UInt32);
    table.add_column("Version", utf::ColumnType::UInt16);
    table.add_column("Revision", utf::ColumnType::UInt16);
    table.add_column("Align", utf::ColumnType::UInt16);
    table.add_column("Sorted", utf::ColumnType::UInt16);
    table.add_column("EnableFileName", utf::ColumnType::UInt16);
    table.add_column("EID", utf::ColumnType::UInt16);
    table.add_column("CpkMode", utf::ColumnType::UInt32);
    table.add_column("Tvers", utf::ColumnType::String);
    table.add_column("Comment", utf::ColumnType::String);
    table.add_column("Codec", utf::ColumnType::UInt32);
    table.add_column("DpkItoc", utf::ColumnType::UInt32);
    table.add_column("EnableTocCrc", utf::ColumnType::UInt16);
    table.add_column("EnableFileCrc", utf::ColumnType::UInt16);
    table.add_column("CrcMode", utf::ColumnType::UInt32);
    table.add_column("CrcTable", utf::ColumnType::VLData);

    const auto row = table.add_row();
    table.set(row, "UpdateDateTime", 0ull).value();
    table.set(row, "FileSize", file_size).value();
    table.set(row, "ContentOffset", content_offset).value();
    table.set(row, "ContentSize", content_size).value();
    table.set(row, "TocOffset", has_toc ? root_chunk_size : 0ull).value();
    table.set(row, "TocSize", has_toc ? toc_chunk_size : 0ull).value();
    table.set(row, "TocCrc", toc_crc).value();
    table.set(row, "HtocOffset", 0ull).value();
    table.set(row, "HtocSize", 0ull).value();
    table.set(row, "EtocOffset", etoc_chunk_offset).value();
    table.set(row, "EtocSize", etoc_chunk_size).value();
    table.set(row, "ItocOffset", has_itoc ? itoc_chunk_offset : 0ull).value();
    table.set(row, "ItocSize", has_itoc ? itoc_chunk_size : 0ull).value();
    table.set(row, "ItocCrc", itoc_crc).value();
    table.set(row, "GtocOffset", has_gtoc ? gtoc_chunk_offset : 0ull).value();
    table.set(row, "GtocSize", has_gtoc ? gtoc_chunk_size : 0ull).value();
    table.set(row, "GtocCrc", gtoc_crc).value();
    table.set(row, "HgtocOffset", 0ull).value();
    table.set(row, "HgtocSize", 0ull).value();
    table.set(row, "EnabledPackedSize", enabled_packed_size).value();
    table.set(row, "EnabledDataSize", enabled_data_size).value();
    table.set(row, "TotalDataSize", content_size).value();
    table.set(row, "Tocs", has_toc ? 1u : 0u).value();
    table.set(row, "Files", static_cast<uint32_t>(m_files.size())).value();
    table.set(row, "Groups", has_gtoc ? 1u : 0u).value();
    table.set(row, "Attrs", has_gtoc ? 1u : 0u).value();
    table.set(row, "TotalFiles", static_cast<uint32_t>(m_files.size())).value();
    table.set(row, "Directories", static_cast<uint32_t>(directories.size())).value();
    table.set(row, "Updates", 0u).value();
    table.set(row, "Version", static_cast<uint16_t>(7)).value();
    table.set(row, "Revision", static_cast<uint16_t>(14)).value();
    table.set(row, "Align", m_options.align).value();
    table.set(row, "Sorted", static_cast<uint16_t>(has_toc ? 1 : 0)).value();
    table.set(row, "EnableFileName", static_cast<uint16_t>(has_toc ? 1 : 0)).value();
    table.set(row, "EID", static_cast<uint16_t>(0)).value();
    table.set(row, "CpkMode", header_mode_value(m_options.preset)).value();
    table.set(row, "Tvers", m_options.tver).value();
    table.set(row, "Comment", m_options.comment).value();
    table.set(row, "Codec", 0u).value();
    table.set(row, "DpkItoc", 0u).value();
    table.set(row, "EnableTocCrc", static_cast<uint16_t>(m_options.enable_crc ? 1 : 0)).value();
    table.set(row, "EnableFileCrc", static_cast<uint16_t>(m_options.enable_crc ? 1 : 0)).value();
    table.set(row, "CrcMode", 0u).value();
    table.set(row, "CrcTable", std::vector<uint8_t>{}).value();
    return table.build();
}

std::vector<uint8_t> Cpk::wrap_chunk(
    std::string_view magic,
    std::span<const uint8_t> table_data,
    bool encrypt_payload
) const {
    constexpr size_t header_size = 0x10;
    std::vector<uint8_t> chunk(header_size + table_data.size(), 0);
    std::copy(magic.begin(), magic.end(), chunk.begin());
    io::write_le<uint32_t>(chunk.data() + 0x04, encrypt_payload ? 0u : 0xFFu);
    io::write_le<uint32_t>(chunk.data() + 0x08, static_cast<uint32_t>(table_data.size()));
    io::write_le<uint32_t>(chunk.data() + 0x0C, 0u);
    std::copy(table_data.begin(), table_data.end(), chunk.begin() + header_size);
    if (encrypt_payload) {
        crypt_utf_payload(std::span<uint8_t>(chunk.data() + header_size, table_data.size()));
    }
    return chunk;
}

int Cpk::compare_archive_paths(std::string_view lhs, std::string_view rhs) {
    auto normalize_char = [](char value) {
        if (value == '\\') {
            return '/';
        }
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    };

    const size_t limit = std::min(lhs.size(), rhs.size());
    for (size_t index = 0; index < limit; ++index) {
        const char lhs_char = normalize_char(lhs[index]);
        const char rhs_char = normalize_char(rhs[index]);
        if (lhs_char != rhs_char) {
            return lhs_char < rhs_char ? -1 : 1;
        }
    }

    if (lhs.size() == rhs.size()) {
        return 0;
    }
    return lhs.size() < rhs.size() ? -1 : 1;
}

void CpkBuilder::add_file(
    const std::filesystem::path& local_path,
    const std::string& cpk_path,
    bool compress,
    std::optional<uint32_t> id
) {
    m_cpk.add_file(local_path, cpk_path, compress, id);
}

std::expected<std::vector<uint8_t>, std::string> CpkBuilder::build(const CpkBuilderOptions& options) {
    m_cpk.set_options(options);
    return m_cpk.save();
}

std::expected<void, std::string> CpkBuilder::build_to_file(
    const std::filesystem::path& output_path,
    const CpkBuilderOptions& options
) {
    m_cpk.set_options(options);
    return m_cpk.save_to_file(output_path);
}

} // namespace cricodecs::cpk
