/**
 * @file cvm_reader.cpp
 * @brief CVM/ROFS image reader.
 */

#include "cvm_container.hpp"

#include <algorithm>
#include <array>
#include <flat_set>
#include <string_view>

#include "cvm_crypto.hpp"
#include "cvm_path.hpp"
#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"
#include "../utilities/string.hpp"

namespace cricodecs::cvm {

namespace {

using io::read_be;
using io::read_le;
using util::align_up;
using util::divide_round_up;
using util::trim_ascii;

constexpr std::array<uint8_t, 4> cvmh_magic = {'C', 'V', 'M', 'H'};
constexpr std::array<uint8_t, 4> zone_magic = {'Z', 'O', 'N', 'E'};
constexpr std::array<uint8_t, 5> iso_magic = {'C', 'D', '0', '0', '1'};
constexpr size_t sector_size = CvmContainer::sector_length();
constexpr size_t cvmh_offset = 0x0000;
constexpr size_t zone_offset = 0x0800;
constexpr uint32_t pvd_sector = 16;
constexpr size_t pvd_root_record_offset = 156;

[[nodiscard]] std::optional<CvmKey> key_from_string(std::string_view key) {
    return key.empty() ? std::nullopt : std::optional<CvmKey>{crypto::calc_key_from_string(key)};
}

[[nodiscard]] std::string read_ascii(std::span<const uint8_t> source, size_t offset, size_t length) {
    return trim_ascii(source.subspan(offset, length));
}

struct IsoDirectoryRecord {
    uint8_t record_length = 0;
    uint8_t ext_attr_length = 0;
    uint32_t extent_sector = 0;
    uint32_t data_length = 0;
    uint8_t flags = 0;
    std::string name;
};

[[nodiscard]] std::string normalize_iso_name(std::string_view raw_name) {
    const size_t version_delimiter = raw_name.find(';');
    if (version_delimiter != std::string_view::npos) {
        raw_name = raw_name.substr(0, version_delimiter);
    }
    return std::string(raw_name);
}

template <typename T>
[[nodiscard]] std::expected<T, std::string> read_iso_both_endian(
    std::span<const uint8_t> source,
    size_t offset,
    std::string_view field_name
) {
    if (offset + sizeof(T) * 2 > source.size()) {
        return std::unexpected("CVM ISO " + std::string(field_name) + " field is out of bounds");
    }

    const T little_endian = read_le<T>(source.data() + offset);
    const T big_endian = read_be<T>(source.data() + offset + sizeof(T));
    if (little_endian != big_endian) {
        return std::unexpected("CVM ISO " + std::string(field_name) + " little-endian and big-endian values disagree");
    }

    return little_endian;
}

[[nodiscard]] std::expected<IsoDirectoryRecord, std::string> parse_iso_directory_record(
    std::span<const uint8_t> source,
    size_t offset
) {
    if (offset + 33 > source.size()) {
        return std::unexpected("CVM ISO directory record header is out of bounds");
    }

    IsoDirectoryRecord record;
    record.record_length = source[offset + 0];
    if (record.record_length == 0) {
        return record;
    }
    if (offset + record.record_length > source.size()) {
        return std::unexpected("CVM ISO directory record exceeds the source size");
    }

    record.ext_attr_length = source[offset + 1];
    record.extent_sector = read_le<uint32_t>(source.data() + offset + 2) + record.ext_attr_length;
    record.data_length = read_le<uint32_t>(source.data() + offset + 10);
    record.flags = source[offset + 25];

    const uint8_t name_length = source[offset + 32];
    if (offset + 33 + name_length > source.size() || 33u + name_length > record.record_length) {
        return std::unexpected("CVM ISO directory name exceeds the record size");
    }

    record.name.assign(
        reinterpret_cast<const char*>(source.data() + offset + 33),
        reinterpret_cast<const char*>(source.data() + offset + 33 + name_length)
    );
    return record;
}

[[nodiscard]] std::expected<void, std::string> parse_iso_directory_tree(
    std::span<const uint8_t> source,
    size_t iso_offset,
    uint32_t extent_sector,
    uint32_t directory_size,
    const std::filesystem::path& current_path,
    std::flat_set<uint32_t>& visited_directories,
    std::vector<CvmEntry>& entries,
    std::vector<CvmDirectoryRecord>& directories
) {
    if (!visited_directories.insert(extent_sector).second) {
        return {};
    }

    directories.push_back({
        .directory_path = current_path,
        .extent_sector = extent_sector,
        .byte_size = directory_size,
        .entries = {},
    });

    const size_t directory_offset = iso_offset + static_cast<size_t>(extent_sector) * sector_size;
    if (directory_offset > source.size() || directory_size > source.size() - directory_offset) {
        return std::unexpected("CVM ISO directory data is out of bounds");
    }

    size_t consumed = 0;
    while (consumed < directory_size) {
        const size_t record_offset = directory_offset + consumed;
        auto record = parse_iso_directory_record(source, record_offset);
        if (!record) {
            return std::unexpected(record.error());
        }

        if (record->record_length == 0) {
            const size_t next_sector_boundary = align_up(consumed + 1, sector_size);
            if (next_sector_boundary <= consumed) {
                break;
            }
            consumed = next_sector_boundary;
            continue;
        }

        consumed += record->record_length;
        if (record->name.size() == 1 && (record->name[0] == '\0' || record->name[0] == '\1')) {
            continue;
        }

        const std::string normalized_name = normalize_iso_name(record->name);
        const std::filesystem::path entry_path = current_path.empty()
            ? std::filesystem::path(normalized_name)
            : current_path / normalized_name;
        const bool is_directory = (record->flags & 0x02u) != 0;

        if (is_directory) {
            auto recurse = parse_iso_directory_tree(
                source,
                iso_offset,
                record->extent_sector,
                record->data_length,
                entry_path,
                visited_directories,
                entries,
                directories
            );
            if (!recurse) {
                return std::unexpected(recurse.error());
            }
            continue;
        }

        entries.push_back({
            .index = static_cast<uint32_t>(entries.size()),
            .path = entry_path,
            .extent_sector = record->extent_sector,
            .size = record->data_length,
        });
    }

    return {};
}

[[nodiscard]] std::string format_recording_date(const std::array<uint8_t, 7>& bytes) {
    if (std::all_of(bytes.begin(), bytes.end(), [](uint8_t value) { return value == 0; })) {
        return {};
    }

    const int year = 1900 + bytes[0];
    const int month = bytes[1];
    const int day = bytes[2];
    const int hour = bytes[3];
    const int minute = bytes[4];
    const int second = bytes[5];
    const int gmt = static_cast<int8_t>(bytes[6]);

    char formatted[64];
    std::snprintf(
        formatted,
        sizeof(formatted),
        "%02d/%02d/%04d %02d:%02d:%02d:00:%d",
        day,
        month,
        year,
        hour,
        minute,
        second,
        gmt
    );
    return formatted;
}

[[nodiscard]] std::expected<void, std::string> validate_primary_volume_descriptor(std::span<const uint8_t> source, size_t iso_offset) {
    const size_t pvd_offset = iso_offset + static_cast<size_t>(pvd_sector) * sector_size;
    if (pvd_offset + sector_size > source.size()) {
        return std::unexpected("CVM primary volume descriptor is out of bounds");
    }
    if (source[pvd_offset + 0] != 0x01u ||
        !std::equal(iso_magic.begin(), iso_magic.end(), source.begin() + static_cast<std::ptrdiff_t>(pvd_offset + 1))) {
        return std::unexpected("CVM parse failed: invalid embedded ISO9660 primary volume descriptor");
    }
    return {};
}

[[nodiscard]] std::expected<uint32_t, std::string> decrypt_scrambled_toc_in_place(
    std::vector<uint8_t>& image,
    size_t iso_offset,
    std::span<const uint8_t, 8> key
) {
    const auto decrypt_sector = [&](uint32_t sector) {
        const size_t offset = iso_offset + static_cast<size_t>(sector) * sector_size;
        crypto::transform_sectors(
            std::span<uint8_t>(image.data() + static_cast<std::ptrdiff_t>(offset), sector_size),
            sector,
            1,
            sector_size,
            key
        );
    };

    decrypt_sector(pvd_sector);
    auto pvd_ok = validate_primary_volume_descriptor(image, iso_offset);
    if (!pvd_ok) {
        return std::unexpected("CVM scramble key did not produce a valid ISO primary volume descriptor");
    }

    std::flat_set<uint32_t> visited_directories;
    std::flat_set<uint32_t> decrypted_sectors{pvd_sector};

    const auto decrypt_directory_tree = [&](
        this auto&& self,
        uint32_t extent_sector,
        uint32_t directory_size
    ) -> std::expected<uint32_t, std::string> {
        const uint32_t sector_count = static_cast<uint32_t>(divide_round_up(directory_size, sector_size));
        if (visited_directories.insert(extent_sector).second) {
            for (uint32_t sector = 0; sector < sector_count; ++sector) {
                const uint32_t absolute_sector = extent_sector + sector;
                if (!decrypted_sectors.insert(absolute_sector).second) {
                    continue;
                }
                decrypt_sector(absolute_sector);
            }

            const size_t directory_offset = iso_offset + static_cast<size_t>(extent_sector) * sector_size;
            size_t consumed = 0;
            while (consumed < directory_size) {
                const size_t record_offset = directory_offset + consumed;
                auto record = parse_iso_directory_record(image, record_offset);
                if (!record) {
                    return std::unexpected(record.error());
                }
                if (record->record_length == 0) {
                    const size_t next_sector_boundary = align_up(consumed + 1, sector_size);
                    if (next_sector_boundary <= consumed) {
                        break;
                    }
                    consumed = next_sector_boundary;
                    continue;
                }

                consumed += record->record_length;
                if (record->name.size() == 1 && (record->name[0] == '\0' || record->name[0] == '\1')) {
                    continue;
                }
                if ((record->flags & 0x02u) != 0) {
                    auto child_end = self(record->extent_sector, record->data_length);
                    if (!child_end) {
                        return std::unexpected(child_end.error());
                    }
                }
            }
        }

        uint32_t end_sector = extent_sector + sector_count;
        const size_t directory_offset = iso_offset + static_cast<size_t>(extent_sector) * sector_size;
        size_t consumed = 0;
        while (consumed < directory_size) {
            const size_t record_offset = directory_offset + consumed;
            auto record = parse_iso_directory_record(image, record_offset);
            if (!record) {
                return std::unexpected(record.error());
            }
            if (record->record_length == 0) {
                const size_t next_sector_boundary = align_up(consumed + 1, sector_size);
                if (next_sector_boundary <= consumed) {
                    break;
                }
                consumed = next_sector_boundary;
                continue;
            }
            consumed += record->record_length;
            if (record->name.size() == 1 && (record->name[0] == '\0' || record->name[0] == '\1')) {
                continue;
            }
            if ((record->flags & 0x02u) != 0) {
                const auto child_sector_count = static_cast<uint32_t>(divide_round_up(record->data_length, sector_size));
                end_sector = std::max(end_sector, record->extent_sector + child_sector_count);
            }
        }
        return end_sector;
    };

    const size_t pvd_offset = iso_offset + static_cast<size_t>(pvd_sector) * sector_size;
    auto root_record = parse_iso_directory_record(image, pvd_offset + pvd_root_record_offset);
    if (!root_record) {
        return std::unexpected(root_record.error());
    }

    auto toc_end = decrypt_directory_tree(root_record->extent_sector, root_record->data_length);
    if (!toc_end) {
        return std::unexpected(toc_end.error());
    }

    for (uint32_t sector = pvd_sector; sector < *toc_end; ++sector) {
        if (!decrypted_sectors.insert(sector).second) {
            continue;
        }
        decrypt_sector(sector);
    }
    return *toc_end;
}

} // namespace

std::expected<CvmContainer, std::string> CvmContainer::load_owned(
    std::vector<uint8_t>&& data,
    std::filesystem::path source_path,
    std::optional<CvmKey> key
) {
    return load_source(io::SourceView::from_owned(std::move(data)), std::move(source_path), key);
}

std::expected<CvmContainer, std::string> CvmContainer::load_source(
    io::SourceView source,
    std::filesystem::path source_path,
    std::optional<CvmKey> key
) {
    CvmContainer container;
    container.m_source = std::move(source);
    container.m_source_path = std::move(source_path);
    if (auto parsed = container.parse(key); !parsed) {
        return std::unexpected(parsed.error());
    }
    return container;
}

std::expected<CvmContainer, std::string> CvmContainer::load_path(
    const std::filesystem::path& path,
    std::optional<CvmKey> key
) {
    auto source = io::SourceView::from_file(path);
    if (!source) {
        return std::unexpected("CVM load failed: " + path.string() + " (" + source.error() + ")");
    }
    return load_source(std::move(*source), path, key);
}

std::expected<CvmContainer, std::string> CvmContainer::load(std::span<const uint8_t> data, std::string_view key) {
    return load_owned(std::vector<uint8_t>(data.begin(), data.end()), {}, key_from_string(key));
}

std::expected<CvmContainer, std::string> CvmContainer::load(std::vector<uint8_t>&& data, std::string_view key) {
    return load_owned(std::move(data), {}, key_from_string(key));
}

std::expected<CvmContainer, std::string> CvmContainer::load(const std::filesystem::path& path, std::string_view key) {
    return load_path(path, key_from_string(key));
}

std::expected<CvmContainer, std::string> CvmContainer::load(std::span<const uint8_t> data, const CvmKey& key) {
    return load_owned(std::vector<uint8_t>(data.begin(), data.end()), {}, key);
}

std::expected<CvmContainer, std::string> CvmContainer::load(std::vector<uint8_t>&& data, const CvmKey& key) {
    return load_owned(std::move(data), {}, key);
}

std::expected<CvmContainer, std::string> CvmContainer::load(const std::filesystem::path& path, const CvmKey& key) {
    return load_path(path, key);
}

std::expected<void, std::string> CvmContainer::parse(std::optional<CvmKey> key) {
    m_header = {};
    m_zone = {};
    m_primary_volume = {};
    m_media = "DVD";
    m_entries.clear();
    m_entry_payloads.clear();
    m_directories.clear();
    m_contents_accessible = true;

    if (m_source.size() < zone_offset + sector_size) {
        return std::unexpected("CVM data is too small");
    }
    if (!std::equal(cvmh_magic.begin(), cvmh_magic.end(), m_source.begin() + static_cast<std::ptrdiff_t>(cvmh_offset))) {
        return std::unexpected("CVM parse failed: invalid CVMH magic");
    }
    if (!std::equal(zone_magic.begin(), zone_magic.end(), m_source.begin() + static_cast<std::ptrdiff_t>(zone_offset))) {
        return std::unexpected("CVM parse failed: invalid ZONE magic");
    }

    m_header.chunk_length = read_be<uint64_t>(m_source.data() + cvmh_offset + 0x04);
    m_header.total_size = read_be<uint64_t>(m_source.data() + cvmh_offset + 0x1C);
    std::copy_n(m_source.data() + cvmh_offset + 0x24, m_header.recording_date.size(), m_header.recording_date.begin());
    m_header.flags = read_be<uint32_t>(m_source.data() + cvmh_offset + 0x30);
    m_header.filesystem_id = read_ascii(m_source, cvmh_offset + 0x34, 4);
    m_header.maker_id = read_ascii(m_source, cvmh_offset + 0x38, 64);
    m_header.sector_table_entry_count = read_be<uint32_t>(m_source.data() + cvmh_offset + 0x80);
    m_header.zone_sector_index = read_be<uint32_t>(m_source.data() + cvmh_offset + 0x84);
    m_header.iso_start_sector = read_be<uint32_t>(m_source.data() + cvmh_offset + 0x88);

    const size_t sector_table_offset = cvmh_offset + 0x100;
    const size_t sector_table_size = static_cast<size_t>(m_header.sector_table_entry_count) * sizeof(uint32_t);
    if (sector_table_offset + sector_table_size > zone_offset) {
        return std::unexpected("CVM sector table exceeds the CVMH chunk");
    }

    m_header.sector_table.resize(m_header.sector_table_entry_count);
    io::read_structs<std::endian::big>(
        m_source.data() + sector_table_offset,
        std::span(m_header.sector_table));

    if (m_header.total_size != m_source.size()) {
        return std::unexpected("CVM total size field does not match the source size");
    }
    if (m_header.filesystem_id != "ROFS") {
        return std::unexpected("CVM parse failed: unsupported filesystem identifier: " + m_header.filesystem_id);
    }

    m_zone.chunk_length = read_be<uint64_t>(m_source.data() + zone_offset + 0x04);
    m_zone.zone_sector = read_be<uint32_t>(m_source.data() + zone_offset + 0x0C);
    m_zone.sector_length_1 = read_be<uint32_t>(m_source.data() + zone_offset + 0x1C);
    m_zone.sector_length_2 = read_be<uint32_t>(m_source.data() + zone_offset + 0x20);
    m_zone.data_sector = read_be<uint32_t>(m_source.data() + zone_offset + 0x24);
    m_zone.data_length = read_be<uint64_t>(m_source.data() + zone_offset + 0x24);
    m_zone.iso_sector = read_be<uint32_t>(m_source.data() + zone_offset + 0x2C);
    m_zone.iso_length = read_be<uint64_t>(m_source.data() + zone_offset + 0x30);

    if (m_zone.iso_sector != m_header.iso_start_sector) {
        return std::unexpected("CVM ISO sector disagrees between CVMH and ZONE");
    }

    const size_t iso_offset = embedded_iso_offset();
    const size_t iso_size = embedded_iso_size();
    if (iso_offset > m_source.size() || iso_size > m_source.size() - iso_offset) {
        return std::unexpected("CVM embedded ISO range is out of bounds");
    }

    if (is_scrambled()) {
        if (!key) {
            auto recovered_key = recover_key(m_source);
            if (!recovered_key) {
                m_contents_accessible = false;
                m_recording_date_text = format_recording_date(m_header.recording_date);
                m_disc_name = default_disc_name(m_source_path, m_primary_volume.volume_identifier);
                return {};
            }
            key = *recovered_key;
        }
        std::vector<uint8_t> decrypted(m_source.begin(), m_source.end());
        auto toc_result = decrypt_scrambled_toc_in_place(decrypted, iso_offset, *key);
        if (!toc_result) {
            return std::unexpected(toc_result.error());
        }
        m_source = io::SourceView::from_owned(std::move(decrypted));
    }

    auto pvd_ok = validate_primary_volume_descriptor(m_source, iso_offset);
    if (!pvd_ok) {
        return std::unexpected(pvd_ok.error());
    }

    const size_t pvd_offset = iso_offset + static_cast<size_t>(pvd_sector) * sector_size;
    m_primary_volume.system_identifier = read_ascii(m_source, pvd_offset + 8, 32);
    m_primary_volume.volume_identifier = read_ascii(m_source, pvd_offset + 40, 32);
    m_primary_volume.volume_set_identifier = read_ascii(m_source, pvd_offset + 190, 128);
    m_primary_volume.publisher_identifier = read_ascii(m_source, pvd_offset + 318, 128);
    m_primary_volume.data_preparer_identifier = read_ascii(m_source, pvd_offset + 446, 128);
    m_primary_volume.application_identifier = read_ascii(m_source, pvd_offset + 574, 128);

    auto volume_space_size = read_iso_both_endian<uint32_t>(m_source, pvd_offset + 80, "volume space size");
    if (!volume_space_size) {
        return std::unexpected(volume_space_size.error());
    }
    m_primary_volume.volume_space_size = *volume_space_size;

    auto logical_block_size = read_iso_both_endian<uint16_t>(m_source, pvd_offset + 128, "logical block size");
    if (!logical_block_size) {
        return std::unexpected(logical_block_size.error());
    }
    m_primary_volume.logical_block_size = *logical_block_size;
    if (m_primary_volume.logical_block_size != sector_size) {
        return std::unexpected("CVM parse failed: unsupported ISO9660 logical block size");
    }
    if (static_cast<uint64_t>(m_primary_volume.volume_space_size) * m_primary_volume.logical_block_size != iso_size) {
        return std::unexpected("CVM ISO volume space size does not match the embedded ISO span");
    }

    auto root_record = parse_iso_directory_record(m_source, pvd_offset + pvd_root_record_offset);
    if (!root_record) {
        return std::unexpected(root_record.error());
    }

    std::flat_set<uint32_t> visited_directories;
    auto parsed_tree = parse_iso_directory_tree(
        m_source,
        iso_offset,
        root_record->extent_sector,
        root_record->data_length,
        {},
        visited_directories,
        m_entries,
        m_directories
    );
    if (!parsed_tree) {
        return std::unexpected(parsed_tree.error());
    }

    std::flat_set<std::string> seen_entry_paths;
    m_entry_payloads.reserve(m_entries.size());
    for (size_t index = 0; index < m_entries.size(); ++index) {
        const std::string lookup_key = normalize_archive_lookup_key(m_entries[index].path);
        if (!seen_entry_paths.insert(lookup_key).second) {
            return std::unexpected("CVM parse failed: duplicate archive path: " + m_entries[index].path.generic_string());
        }
        m_entries[index].index = static_cast<uint32_t>(index);
        m_entry_payloads.push_back({
            .source_path = {},
            .source_offset = iso_offset + static_cast<size_t>(m_entries[index].extent_sector) * sector_size,
            .owned_bytes = std::nullopt,
        });
    }

    m_recording_date_text = format_recording_date(m_header.recording_date);
    m_disc_name = default_disc_name(m_source_path, m_primary_volume.volume_identifier);
    return {};
}

} // namespace cricodecs::cvm
