/**
 * @file afs_reader.cpp
 * @brief Classic AFS reader and extractor.
 *
 * Parsing follows the reviewed CRI AfsLink/afslnk archive layout, including the
 * optional directory table and sparse file-ID slots. C++23 port by Youjose.
 */

#include "afs_container.hpp"

#include <algorithm>
#include <numeric>

#include "afs_format.hpp"
#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"

namespace cricodecs::afs {

namespace {

using io::read_le;
using util::align_up;

[[nodiscard]] uint32_t infer_alignment(
    const std::vector<AfsEntry>& entries,
    std::optional<uint32_t> directory_offset
) {
    uint32_t inferred = 0;
    for (const auto& entry : entries) {
        if (entry.present && entry.offset != 0) {
            inferred = inferred == 0 ? entry.offset : std::gcd(inferred, entry.offset);
        }
    }

    if (directory_offset && *directory_offset != 0) {
        inferred = inferred == 0 ? *directory_offset : std::gcd(inferred, *directory_offset);
    }

    return inferred == 0 ? AfsContainer::DEFAULT_ALIGNMENT : inferred;
}

} // namespace

std::expected<AfsContainer, std::string> AfsContainer::load(std::span<const uint8_t> data) {
    return load(data, nullptr);
}

std::expected<AfsContainer, std::string> AfsContainer::load(
    std::span<const uint8_t> data,
    io::SourceView::Owner owner
) {
    AfsContainer container;
    container.m_source = io::SourceView(data, std::move(owner));
    return container.parse().transform([&] { return std::move(container); });
}

std::expected<AfsContainer, std::string> AfsContainer::load(const std::filesystem::path& path) {
    auto source = io::SourceView::from_file(path);
    if (!source) {
        return std::unexpected("AFS load failed: failed to open " + path.string() + " (" + source.error() + ")");
    }
    return load(source->bytes, std::move(source->owner)).transform([&](AfsContainer container) {
        container.m_source_path = path;
        return container;
    });
}

std::expected<void, std::string> AfsContainer::parse() {
    m_entries.clear();
    m_payloads.clear();
    m_directory_table_offset.reset();
    m_directory_table_size.reset();
    m_first_payload_offset.reset();
    m_emit_directory_table = false;
    m_alignment = DEFAULT_ALIGNMENT;

    if (m_source.size() < sizeof(detail::AfsHeader)) {
        return std::unexpected("AFS data is too small");
    }
    const auto header = read_le<detail::AfsHeader>(m_source.data());
    if (header.magic != detail::afs_magic.le_value()) {
        return std::unexpected("AFS parse failed: invalid magic");
    }
    const uint32_t entry_count = header.entry_count;
    if (entry_count == 0) {
        return std::unexpected("AFS entry count is invalid");
    }

    const uint64_t table_end = sizeof(detail::AfsHeader) +
        static_cast<uint64_t>(entry_count) * sizeof(detail::AfsRange);
    if (table_end > m_source.size()) {
        return std::unexpected("AFS index table exceeds the source size");
    }

    m_entries.reserve(entry_count);
    const auto* table = m_source.data() + sizeof(detail::AfsHeader);
    for (uint32_t index = 0; index < entry_count; ++index) {
        const auto record = read_le<detail::AfsRange>(table + index * sizeof(detail::AfsRange));

        AfsEntry entry;
        entry.index = index;
        entry.offset = record.offset;
        entry.size = record.size;
        entry.present = entry.offset != 0 || entry.size != 0;

        if (entry.present &&
            (entry.offset > m_source.size() || entry.size > m_source.size() - entry.offset)) {
            return std::unexpected("AFS entry data is out of bounds");
        }

        entry.type = entry.present ? detail::detect_entry_type(m_source, entry.offset, entry.size) : AfsEntryType::unknown;
        m_entries.push_back(entry);
    }

    if (table_end + 0x08 <= m_source.size()) {
        const auto directory = read_le<detail::AfsRange>(m_source.data() + table_end);
        const uint32_t directory_offset = directory.offset;
        const uint32_t directory_size = directory.size;

        if (directory_offset != 0 && directory_size != 0 &&
            directory_offset <= m_source.size() &&
            directory_size <= m_source.size() - directory_offset) {
            m_directory_table_offset = directory_offset;
            m_directory_table_size = directory_size;
            m_emit_directory_table = true;

            const uint64_t required_size = static_cast<uint64_t>(m_entries.size()) * detail::directory_entry_size;
            if (directory_size >= required_size) {
                for (size_t index = 0; index < m_entries.size(); ++index) {
                    const uint8_t* record = m_source.data() + directory_offset + index * detail::directory_entry_size;
                    m_entries[index].name = detail::parse_name(record, detail::directory_name_size);
                    std::copy_n(record + detail::directory_name_size, m_entries[index].directory_metadata.size(), m_entries[index].directory_metadata.begin());
                }
            }
        }
    }

    m_alignment = infer_alignment(m_entries, m_directory_table_offset);
    const uint32_t first_payload_offset = detail::first_present_source_offset(m_source);
    if (first_payload_offset != 0) {
        m_first_payload_offset = first_payload_offset;
    }

    return {};
}

std::expected<std::span<const uint8_t>, std::string> AfsContainer::file_data(uint32_t index) const {
    if (index >= m_entries.size()) {
        return std::unexpected("AFS entry index is out of range");
    }

    const auto& entry = m_entries[index];
    if (!entry.present) {
        return std::unexpected("AFS entry slot is empty");
    }
    if (index < m_payloads.size() && m_payloads[index]) {
        return std::span<const uint8_t>(*m_payloads[index]);
    }
    return m_source.subspan(entry.offset, entry.size);
}

std::expected<void, std::string> AfsContainer::export_stream(
    uint32_t index,
    const std::filesystem::path& output_path
) const {
    auto data = file_data(index);
    if (!data) {
        return std::unexpected(data.error());
    }

    return io::write_file_bytes(output_path, *data, "AFS export failed");
}

std::expected<void, std::string> AfsContainer::extract(const std::filesystem::path& output_dir) const {
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_dir, filesystem_error);
    if (filesystem_error) {
        return std::unexpected("AFS export failed: could not create output directory: " + filesystem_error.message());
    }

    for (const auto& entry : m_entries) {
        if (!entry.present) {
            continue;
        }
        auto export_result = export_stream(entry.index, output_dir / entry.suggested_path());
        if (!export_result) {
            return std::unexpected(export_result.error());
        }
    }

    return {};
}

} // namespace cricodecs::afs
