/**
 * @file acx_reader.cpp
 * @brief ACX reader for the reviewed flat archive layout.
 *
 * Reader behavior is checked against official `adxcat` output.
 * The C++23 implementation is CriCodecs work by Youjose.
 */

#include "acx_container.hpp"

#include "../utilities/io.hpp"

namespace cricodecs::acx {

namespace {

using io::read_be;

constexpr uint32_t max_reasonable_entries = 0x10000;
constexpr uint32_t adx_signature_mask = 0xFFFF0000u;
constexpr uint32_t adx_signature_value = 0x80000000u;
constexpr uint32_t ogg_magic = io::FourCC{"OggS"}.be_value();

[[nodiscard]] AcxEntryType detect_entry_type(std::span<const uint8_t> source, uint32_t offset, uint32_t size) {
    if (offset > source.size() || size > source.size() - offset || size < sizeof(uint32_t)) {
        return AcxEntryType::unknown;
    }

    const uint32_t magic = read_be<uint32_t>(source.data() + offset);
    if ((magic & adx_signature_mask) == adx_signature_value) {
        return AcxEntryType::adx;
    }
    if (magic == ogg_magic) {
        return AcxEntryType::ogg;
    }
    return AcxEntryType::unknown;
}

} // namespace

std::expected<AcxContainer, std::string> AcxContainer::load(std::span<const uint8_t> data) {
    return load(data, nullptr);
}

std::expected<AcxContainer, std::string> AcxContainer::load(
    std::span<const uint8_t> data,
    io::SourceView::Owner owner
) {
    AcxContainer container;
    container.m_source = io::SourceView(data, std::move(owner));
    if (auto result = container.parse(); !result) {
        return std::unexpected(result.error());
    }
    return container;
}

std::expected<AcxContainer, std::string> AcxContainer::load(const std::filesystem::path& path) {
    AcxContainer container;
    if (auto result = container.m_reader.open(path); !result) {
        return std::unexpected("ACX load failed: failed to open " + path.string() + " (" + result.error() + ")");
    }
    container.m_source_path = path;
    container.m_source = io::SourceView(container.m_reader.data());
    if (auto result = container.parse(); !result) {
        return std::unexpected(result.error());
    }
    return container;
}

std::expected<void, std::string> AcxContainer::parse() {
    m_entries.clear();

    if (m_source.size() < 8) {
        return std::unexpected("ACX data is too small");
    }
    if (read_be<uint32_t>(m_source.data()) != 0) {
        return std::unexpected("ACX parse failed: invalid header marker");
    }

    const uint32_t entry_count = read_be<uint32_t>(m_source.data() + 0x04);
    if (entry_count == 0 || entry_count > max_reasonable_entries) {
        return std::unexpected("ACX entry count is invalid");
    }

    const uint64_t table_end = 0x08ull + static_cast<uint64_t>(entry_count) * 0x08ull;
    if (table_end > m_source.size()) {
        return std::unexpected("ACX table exceeds the source size");
    }

    m_entries.reserve(entry_count);
    for (uint32_t index = 0; index < entry_count; ++index) {
        const size_t entry_offset = 0x08u + static_cast<size_t>(index) * 0x08u;

        AcxEntry entry;
        entry.index = index;
        entry.offset = read_be<uint32_t>(m_source.data() + entry_offset + 0x00);
        entry.size = read_be<uint32_t>(m_source.data() + entry_offset + 0x04);

        if (entry.offset > m_source.size() || entry.size > m_source.size() - entry.offset) {
            return std::unexpected("ACX entry data is out of bounds");
        }

        entry.type = detect_entry_type(m_source, entry.offset, entry.size);
        m_entries.push_back(entry);
    }

    return {};
}

std::expected<std::span<const uint8_t>, std::string> AcxContainer::file_data(uint32_t index) const {
    if (index >= m_entries.size()) {
        return std::unexpected("ACX entry index is out of range");
    }
    const auto& entry = m_entries[index];
    return m_source.subspan(entry.offset, entry.size);
}

std::expected<void, std::string> AcxContainer::export_stream(
    uint32_t index,
    const std::filesystem::path& output_path
) const {
    auto data = file_data(index);
    if (!data) {
        return std::unexpected(data.error());
    }

    return io::write_file_bytes(output_path, *data, "ACX export failed");
}

std::expected<void, std::string> AcxContainer::export_all(const std::filesystem::path& output_dir) const {
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_dir, filesystem_error);
    if (filesystem_error) {
        return std::unexpected("ACX export failed: could not create output directory: " + filesystem_error.message());
    }

    for (const auto& entry : m_entries) {
        auto export_result = export_stream(entry.index, output_dir / entry.suggested_path());
        if (!export_result) {
            return std::unexpected(export_result.error());
        }
    }

    return {};
}

} // namespace cricodecs::acx
