/**
 * @file cvm_volume_set.cpp
 * @brief CVM/ROFS mounted-volume helper surface.
 */

#include "cvm_volume_set.hpp"

#include <algorithm>
#include <array>

#include "cvm_path.hpp"
#include "../utilities/io_endian.hpp"
#include "../utilities/numeric.hpp"
#include "../utilities/string.hpp"

namespace cricodecs::cvm {

namespace {

using util::align_up;
using util::divide_round_up;
using util::uppercase_ascii;

constexpr size_t rofs_volume_name_max = 8;
constexpr uint8_t rofs_attribute_directory = 0x02u;
constexpr size_t rofs_dirrec_header_size = 24;
constexpr size_t rofs_dirrec_entry_size = 48;
constexpr size_t rofs_dirrec_volume_name_size = 9;
constexpr size_t rofs_dirrec_file_name_size = 32;
constexpr size_t iso_sector_size = 0x800;

[[nodiscard]] std::string normalize_volume_name(std::string_view volume_name) {
    return uppercase_ascii(std::string(volume_name));
}

[[nodiscard]] std::expected<std::string, std::string> validate_volume_name(std::string_view volume_name) {
    const std::string normalized = normalize_volume_name(volume_name);
    if (normalized.empty()) {
        return std::unexpected("CVM volume name must not be empty");
    }
    if (normalized.size() > rofs_volume_name_max) {
        return std::unexpected("CVM volume name exceeds the ROFS 8-character limit: " + normalized);
    }
    if (normalized == "ROFS") {
        return std::unexpected("CVM volume name 'ROFS' is reserved by the official ROFS runtime");
    }
    return normalized;
}

[[nodiscard]] std::optional<std::pair<std::string, std::filesystem::path>> split_runtime_volume_path(
    const std::filesystem::path& runtime_path
) {
    const std::string normalized = runtime_path.lexically_normal().generic_string();
    const size_t delimiter = normalized.find(':');
    if (delimiter == std::string::npos) {
        return std::nullopt;
    }

    std::string volume_name = normalize_volume_name(normalized.substr(0, delimiter));
    std::filesystem::path archive_path = normalized.substr(delimiter + 1);
    return std::pair<std::string, std::filesystem::path>{std::move(volume_name), std::move(archive_path)};
}

[[nodiscard]] std::expected<std::pair<const CvmMountedVolume*, std::filesystem::path>, std::string> resolve_volume_and_archive_path(
    const CvmVolumeSet& volume_set,
    const std::filesystem::path& runtime_path
) {
    if (const auto split_path = split_runtime_volume_path(runtime_path); split_path.has_value()) {
        const auto* volume = volume_set.find_volume(split_path->first);
        if (volume == nullptr) {
            return std::unexpected("CVM runtime path volume is not mounted: " + split_path->first);
        }
        return std::pair<const CvmMountedVolume*, std::filesystem::path>{
            volume,
            resolve_directory_relative_path(volume->current_directory, split_path->second),
        };
    }

    if (!volume_set.default_volume_name().has_value()) {
        return std::unexpected(
            "CVM runtime path requires an explicit mounted volume or a configured default volume: " +
            runtime_path.generic_string()
        );
    }

    const auto* volume = volume_set.find_volume(*volume_set.default_volume_name());
    if (volume == nullptr) {
        return std::unexpected("CVM default volume is not mounted: " + std::string(*volume_set.default_volume_name()));
    }
    return std::pair<const CvmMountedVolume*, std::filesystem::path>{
        volume,
        resolve_directory_relative_path(volume->current_directory, runtime_path),
    };
}

[[nodiscard]] uint32_t sector_count_for_byte_size(uint64_t byte_size) {
    // ROFS uses 0x800-byte sectors; round declared extents to whole sectors.
    return static_cast<uint32_t>(divide_round_up(byte_size, iso_sector_size));
}

[[nodiscard]] std::expected<const CvmMountedVolume*, std::string> validate_range_handle(
    const CvmVolumeSet& volume_set,
    const CvmRofsRangeHandle& handle
) {
    if (handle.volume_name.empty()) {
        return std::unexpected("CVM ROFS range handle is closed");
    }

    const auto* volume = volume_set.find_volume(handle.volume_name);
    if (volume == nullptr) {
        return std::unexpected("CVM volume is not mounted: " + handle.volume_name);
    }
    if (handle.current_sector > handle.sector_count) {
        return std::unexpected("CVM ROFS range handle current sector exceeds its sector count");
    }
    if (handle.last_transfer_status == CvmRofsTransferStatus::transferring) {
        return std::unexpected("CVM ROFS range handle cannot report an in-progress async transfer state");
    }

    auto range_validation = volume->image.sector_range_data(handle.start_sector, handle.sector_count);
    if (!range_validation) {
        return std::unexpected(
            "CVM ROFS sector range is out of bounds for volume '" + handle.volume_name + "': start_sector=" +
            std::to_string(handle.start_sector) + ", sector_count=" + std::to_string(handle.sector_count)
        );
    }

    return volume;
}

using io::read_le;
using io::write_le;

struct ParsedRofsDirectoryRecord {
    uint32_t dir_num = 0;
    uint32_t max_ent = 0;
    uint32_t dir_fad = 0;
    std::string volume_name;
};

struct ParsedIsoDirectoryRecord {
    uint32_t extent_sector = 0;
    uint32_t data_length = 0;
};

struct RofsDirectoryContext {
    const CvmMountedVolume* volume;
    CvmDirectoryRecord directory;
};

[[nodiscard]] std::expected<ParsedRofsDirectoryRecord, std::string> parse_rofs_directory_record_header(
    std::span<const uint8_t> buffer
) {
    if (buffer.size() < rofs_dirrec_header_size) {
        return std::unexpected("CVM ROFS directory record buffer is too small");
    }

    ParsedRofsDirectoryRecord parsed;
    parsed.dir_num = read_le<uint32_t>(buffer.data() + 0);
    parsed.max_ent = read_le<uint32_t>(buffer.data() + 4);
    parsed.dir_fad = read_le<uint32_t>(buffer.data() + 8);

    if (parsed.max_ent < parsed.dir_num) {
        return std::unexpected("CVM ROFS directory record has dir_num larger than max_ent");
    }
    const size_t required_size = rofs_dirrec_header_size + static_cast<size_t>(parsed.max_ent) * rofs_dirrec_entry_size;
    if (buffer.size() < required_size) {
        return std::unexpected("CVM ROFS directory record buffer is smaller than its declared max_ent size");
    }

    const auto volume_span = buffer.subspan(12, rofs_dirrec_volume_name_size);
    size_t length = 0;
    while (length < volume_span.size() && volume_span[length] != 0) {
        ++length;
    }
    parsed.volume_name.assign(reinterpret_cast<const char*>(volume_span.data()), length);
    if (parsed.volume_name.empty()) {
        return std::unexpected("CVM ROFS directory record is missing a volume name");
    }
    parsed.volume_name = normalize_volume_name(parsed.volume_name);

    return parsed;
}

[[nodiscard]] std::expected<RofsDirectoryContext, std::string> resolve_rofs_directory_context(
    const CvmVolumeSet& volume_set,
    std::span<const uint8_t> buffer
) {
    const auto parsed = parse_rofs_directory_record_header(buffer);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    const auto* volume = volume_set.find_volume(parsed->volume_name);
    if (!volume) {
        return std::unexpected("CVM ROFS directory record volume is not mounted: " + parsed->volume_name);
    }

    auto directory = volume->image.directory_record_from_extent_sector(parsed->dir_fad);
    if (!directory) {
        return std::unexpected(directory.error());
    }
    return RofsDirectoryContext{volume, std::move(*directory)};
}

[[nodiscard]] std::expected<std::vector<CvmRofsFileInfo>, std::string> parse_rofs_directory_entries(
    std::span<const uint8_t> buffer
) {
    const auto parsed = parse_rofs_directory_record_header(buffer);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    std::vector<CvmRofsFileInfo> info;
    info.reserve(parsed->dir_num);
    for (size_t index = 0; index < parsed->dir_num; ++index) {
        const size_t entry_offset = rofs_dirrec_header_size + index * rofs_dirrec_entry_size;
        const uint64_t size = static_cast<uint64_t>(read_le<uint32_t>(buffer.data() + entry_offset + 0)) |
            (static_cast<uint64_t>(read_le<uint32_t>(buffer.data() + entry_offset + 4)) << 32);
        const uint8_t attributes = buffer[entry_offset + 12];
        const auto name_span = buffer.subspan(entry_offset + 14, rofs_dirrec_file_name_size);

        size_t name_length = 0;
        while (name_length < name_span.size() && name_span[name_length] != 0) {
            ++name_length;
        }
        if (name_length == 0) {
            return std::unexpected(
                "CVM ROFS directory record entry " + std::to_string(index) + " is missing a file name"
            );
        }

        info.push_back({
            .name = std::string(reinterpret_cast<const char*>(name_span.data()), name_length),
            .size = size,
            .is_directory = (attributes & rofs_attribute_directory) != 0,
        });
    }

    return info;
}

[[nodiscard]] std::expected<ParsedIsoDirectoryRecord, std::string> parse_iso_directory_record_header(
    std::span<const uint8_t> buffer
) {
    if (buffer.size() < 34) {
        return std::unexpected("CVM ISO directory record buffer is too small");
    }

    const uint8_t record_length = buffer[0];
    if (record_length == 0) {
        return std::unexpected("CVM ISO directory record buffer starts with an empty record");
    }
    if (buffer.size() < record_length) {
        return std::unexpected("CVM ISO directory record buffer is smaller than its first record");
    }

    ParsedIsoDirectoryRecord parsed;
    const uint8_t ext_attr_length = buffer[1];
    parsed.extent_sector = read_le<uint32_t>(buffer.data() + 2) + ext_attr_length;
    parsed.data_length = read_le<uint32_t>(buffer.data() + 10);

    // ROFS directory records are stored in 2048-byte ISO sectors, so callers get
    // the padded span even when the final active directory bytes are shorter.
    const size_t padded_size = static_cast<size_t>(align_up(static_cast<uint64_t>(parsed.data_length), iso_sector_size));
    if (buffer.size() < padded_size) {
        return std::unexpected("CVM ISO directory record buffer is smaller than its declared sector span");
    }

    return parsed;
}

[[nodiscard]] std::expected<std::span<const uint8_t>, std::string> active_iso_directory_record_bytes(
    std::span<const uint8_t> iso_directory_record,
    uint32_t iso_directory_sector_count
) {
    if (iso_directory_sector_count == 0) {
        return std::unexpected("CVM ISO directory record sector count must be greater than zero");
    }

    const size_t active_size = static_cast<size_t>(iso_directory_sector_count) * iso_sector_size;
    if (active_size > iso_directory_record.size()) {
        return std::unexpected(
            "CVM ISO directory record buffer is smaller than the requested sector count: buffer_size=" +
            std::to_string(iso_directory_record.size()) + ", sector_count=" + std::to_string(iso_directory_sector_count)
        );
    }

    return iso_directory_record.first(active_size);
}

[[nodiscard]] std::expected<uint32_t, std::string> entry_extent_sector(
    const CvmContainer& image,
    const CvmDirectoryEntry& entry
) {
    if (entry.is_directory) {
        auto directory = image.directory_record(entry.archive_path);
        if (!directory) {
            return std::unexpected(directory.error());
        }
        return directory->extent_sector;
    }

    const auto* file = image.find_entry(entry.archive_path);
    if (file == nullptr) {
        return std::unexpected("CVM entry path was not found while building ROFS directory record: " + entry.archive_path.generic_string());
    }
    return file->extent_sector;
}

[[nodiscard]] std::expected<CvmRofsRangeHandle, std::string> make_range_handle(
    const CvmVolumeSet& volume_set,
    std::string_view volume_name,
    uint32_t start_sector,
    uint32_t sector_count,
    uint64_t byte_size
) {
    return volume_set.open_range(volume_name, start_sector, sector_count)
        .transform([&](CvmRofsRangeHandle handle) {
            handle.byte_size = byte_size;
            return handle;
        });
}

[[nodiscard]] std::vector<CvmRofsFileInfo> make_rofs_file_info(const CvmDirectoryRecord& directory) {
    std::vector<CvmRofsFileInfo> info;
    info.reserve(directory.entries.size());
    for (const auto& entry : directory.entries) {
        info.push_back({entry.name, entry.size, entry.is_directory});
    }
    return info;
}

} // namespace

std::optional<std::string_view> CvmVolumeSet::default_volume_name() const noexcept {
    if (!m_default_volume_name.has_value()) {
        return std::nullopt;
    }
    return *m_default_volume_name;
}

std::expected<std::string, std::string> CvmVolumeSet::default_volume() const {
    if (!m_default_volume_name.has_value()) {
        return std::unexpected("CVM default volume is not configured");
    }
    return *m_default_volume_name;
}

std::expected<void, std::string> CvmVolumeSet::mount(std::string_view volume_name, CvmContainer&& image) {
    auto validated_name = validate_volume_name(volume_name);
    if (!validated_name) {
        return std::unexpected(validated_name.error());
    }
    if (find_volume(*validated_name) != nullptr) {
        return std::unexpected("CVM volume is already mounted: " + *validated_name);
    }

    m_volumes.push_back({
        .name = *validated_name,
        .image = std::move(image),
        .current_directory = {},
        .scramble_handle_token = m_next_scramble_handle_token++,
    });
    return {};
}

std::expected<void, std::string> CvmVolumeSet::switch_image(std::string_view volume_name, CvmContainer&& image) {
    auto validated_name = validate_volume_name(volume_name);
    if (!validated_name) {
        return std::unexpected(validated_name.error());
    }

    const auto it = std::find_if(m_volumes.begin(), m_volumes.end(), [&](const CvmMountedVolume& volume) {
        return volume.name == *validated_name;
    });
    if (it == m_volumes.end()) {
        return std::unexpected("CVM volume is not mounted: " + *validated_name);
    }

    it->image = std::move(image);
    if (!it->current_directory.empty()) {
        auto directory = it->image.directory_record(it->current_directory);
        if (!directory) {
            it->current_directory.clear();
        }
    }

    return {};
}

std::expected<void, std::string> CvmVolumeSet::unmount(std::string_view volume_name) {
    auto validated_name = validate_volume_name(volume_name);
    if (!validated_name) {
        return std::unexpected(validated_name.error());
    }

    const auto it = std::find_if(m_volumes.begin(), m_volumes.end(), [&](const CvmMountedVolume& volume) {
        return volume.name == *validated_name;
    });
    if (it == m_volumes.end()) {
        return std::unexpected("CVM volume is not mounted: " + *validated_name);
    }

    m_volumes.erase(it);
    if (m_default_volume_name == *validated_name) {
        m_default_volume_name.reset();
    }
    return {};
}

std::expected<void, std::string> CvmVolumeSet::set_default_volume(std::string_view volume_name) {
    auto validated_name = validate_volume_name(volume_name);
    if (!validated_name) {
        return std::unexpected(validated_name.error());
    }
    if (find_volume(*validated_name) == nullptr) {
        return std::unexpected("CVM default volume is not mounted: " + *validated_name);
    }

    m_default_volume_name = *validated_name;
    return {};
}

std::expected<void, std::string> CvmVolumeSet::set_current_directory(const std::filesystem::path& runtime_path) {
    const auto resolved = resolve_volume_and_archive_path(*this, runtime_path);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }

    const auto volume_index = static_cast<size_t>(resolved->first - m_volumes.data());
    auto directory = m_volumes[volume_index].image.directory_record(resolved->second);
    if (!directory) {
        return std::unexpected(directory.error());
    }

    m_volumes[volume_index].current_directory = directory->directory_path;
    return {};
}

std::expected<void, std::string> CvmVolumeSet::set_current_directory(
    std::span<const uint8_t> rofs_directory_record
) {
    const auto context = resolve_rofs_directory_context(*this, rofs_directory_record);
    if (!context) {
        return std::unexpected(context.error());
    }
    const auto volume_index = static_cast<size_t>(context->volume - m_volumes.data());
    m_volumes[volume_index].current_directory = context->directory.directory_path;
    return {};
}

std::expected<void, std::string> CvmVolumeSet::set_current_directory_iso(
    std::string_view volume_name,
    std::span<const uint8_t> iso_directory_record
) {
    if (iso_directory_record.size() % iso_sector_size != 0) {
        return std::unexpected(
            "CVM ISO directory record buffer size must be a multiple of 2048 bytes: " +
            std::to_string(iso_directory_record.size())
        );
    }

    return set_current_directory_iso(
        volume_name,
        iso_directory_record,
        static_cast<uint32_t>(iso_directory_record.size() / iso_sector_size)
    );
}

std::expected<void, std::string> CvmVolumeSet::set_current_directory_iso(
    std::string_view volume_name,
    std::span<const uint8_t> iso_directory_record,
    uint32_t iso_directory_sector_count
) {
    const auto* volume = find_volume(volume_name);
    if (volume == nullptr) {
        return std::unexpected("CVM volume is not mounted: " + normalize_volume_name(volume_name));
    }

    const auto active_bytes = active_iso_directory_record_bytes(iso_directory_record, iso_directory_sector_count);
    if (!active_bytes) {
        return std::unexpected(active_bytes.error());
    }

    const auto parsed = parse_iso_directory_record_header(*active_bytes);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    auto directory = volume->image.directory_record_from_extent_sector(parsed->extent_sector);
    if (!directory) {
        return std::unexpected(directory.error());
    }

    // ROFS directory spans are padded to 2048-byte sector units; compare
    // against the full mounted span.
    const size_t padded_size = static_cast<size_t>(align_up(directory->byte_size, iso_sector_size));
    if (padded_size != active_bytes->size()) {
        return std::unexpected(
            "CVM ISO directory record buffer size does not match mounted directory sector span for '" +
            directory->directory_path.generic_string() + "'"
        );
    }

    const auto volume_index = static_cast<size_t>(volume - m_volumes.data());
    m_volumes[volume_index].current_directory = directory->directory_path;
    return {};
}

std::expected<void, std::string> CvmVolumeSet::set_current_directory_iso(
    std::string_view volume_name,
    std::span<uint8_t> iso_directory_record,
    CvmRofsScrambleInfo& scramble_info
) {
    if (iso_directory_record.size() % iso_sector_size != 0) {
        return std::unexpected(
            "CVM ISO directory record buffer size must be a multiple of 2048 bytes: " +
            std::to_string(iso_directory_record.size())
        );
    }

    return set_current_directory_iso(
        volume_name,
        iso_directory_record,
        static_cast<uint32_t>(iso_directory_record.size() / iso_sector_size),
        scramble_info
    );
}

std::expected<void, std::string> CvmVolumeSet::set_current_directory_iso(
    std::string_view volume_name,
    std::span<uint8_t> iso_directory_record,
    uint32_t iso_directory_sector_count,
    CvmRofsScrambleInfo& scramble_info
) {
    const auto active_bytes = active_iso_directory_record_bytes(
        std::span<const uint8_t>(iso_directory_record.data(), iso_directory_record.size()),
        iso_directory_sector_count
    );
    if (!active_bytes) {
        return std::unexpected(active_bytes.error());
    }

    auto mutable_active_bytes = iso_directory_record.first(active_bytes->size());
    auto descramble_result = descramble(mutable_active_bytes, scramble_info);
    if (!descramble_result) {
        return std::unexpected(descramble_result.error());
    }
    return set_current_directory_iso(
        volume_name,
        std::span<const uint8_t>(iso_directory_record.data(), iso_directory_record.size()),
        iso_directory_sector_count
    );
}

std::expected<CvmRofsRangeHandle, std::string> CvmVolumeSet::open_range(
    std::string_view volume_name,
    uint32_t start_sector,
    uint32_t sector_count
) const {
    const auto* volume = find_volume(volume_name);
    if (volume == nullptr) {
        return std::unexpected("CVM volume is not mounted: " + normalize_volume_name(volume_name));
    }

    auto sector_data = volume->image.sector_range_data(start_sector, sector_count);
    if (!sector_data) {
        return std::unexpected(
            "CVM ROFS sector range is out of bounds for volume '" + volume->name + "': start_sector=" +
            std::to_string(start_sector) + ", sector_count=" + std::to_string(sector_count)
        );
    }

    return CvmRofsRangeHandle{
        .volume_name = volume->name,
        .start_sector = start_sector,
        .sector_count = sector_count,
        .current_sector = 0,
        .byte_size = static_cast<uint64_t>(sector_count) * rofs_sector_length(),
    };
}

std::expected<CvmRofsRangeHandle, std::string> CvmVolumeSet::open_file(
    const std::filesystem::path& runtime_path
) const {
    const auto resolved = resolve_volume_and_archive_path(*this, runtime_path);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }

    const auto* entry = resolved->first->image.find_entry(resolved->second);
    if (entry != nullptr) {
        return make_range_handle(
            *this,
            resolved->first->name,
            entry->extent_sector,
            sector_count_for_byte_size(entry->size),
            entry->size
        );
    }

    auto directory = resolved->first->image.directory_record(resolved->second);
    if (!directory) {
        return std::unexpected("CVM runtime file path was not found: " + runtime_path.generic_string());
    }

    return make_range_handle(
        *this,
        resolved->first->name,
        directory->extent_sector,
        sector_count_for_byte_size(directory->byte_size),
        directory->byte_size
    );
}

std::expected<CvmRofsRangeHandle, std::string> CvmVolumeSet::open_file(
    const std::filesystem::path& relative_path,
    std::span<const uint8_t> rofs_directory_record
) const {
    const auto context = resolve_rofs_directory_context(*this, rofs_directory_record);
    if (!context) {
        return std::unexpected(context.error());
    }

    const auto* entry = context->volume->image.find_entry(relative_path, context->directory);
    if (entry != nullptr) {
        return make_range_handle(
            *this,
            context->volume->name,
            entry->extent_sector,
            sector_count_for_byte_size(entry->size),
            entry->size
        );
    }

    const auto resolved_path = resolve_directory_relative_path(context->directory.directory_path, relative_path);
    auto resolved_directory = context->volume->image.directory_record(resolved_path);
    if (!resolved_directory) {
        return std::unexpected(
            "CVM file path was not found relative to ROFS directory record '" +
            context->directory.directory_path.generic_string() + "': " + relative_path.generic_string()
        );
    }

    return make_range_handle(
        *this,
        context->volume->name,
        resolved_directory->extent_sector,
        sector_count_for_byte_size(resolved_directory->byte_size),
        resolved_directory->byte_size
    );
}

std::expected<uint32_t, std::string> CvmVolumeSet::seek(
    CvmRofsRangeHandle& handle,
    int32_t sector_offset,
    CvmRofsSeekMode seek_mode
) const {
    auto validated = validate_range_handle(*this, handle);
    if (!validated) {
        handle.last_transfer_status = CvmRofsTransferStatus::error;
        handle.last_transfer_sector_count = 0;
        return std::unexpected(validated.error());
    }

    int64_t base = 0;
    switch (seek_mode) {
    case CvmRofsSeekMode::set:
        base = 0;
        break;
    case CvmRofsSeekMode::current:
        base = handle.current_sector;
        break;
    case CvmRofsSeekMode::end:
        base = handle.sector_count;
        break;
    }

    const int64_t next_sector = base + sector_offset;
    if (next_sector < 0 || next_sector > static_cast<int64_t>(handle.sector_count)) {
        handle.last_transfer_status = CvmRofsTransferStatus::error;
        handle.last_transfer_sector_count = 0;
        return std::unexpected(
            "CVM ROFS sector seek is out of bounds for volume '" + handle.volume_name + "': " +
            std::to_string(next_sector)
        );
    }

    handle.current_sector = static_cast<uint32_t>(next_sector);
    handle.last_transfer_sector_count = 0;
    handle.last_transfer_status = CvmRofsTransferStatus::idle;
    return handle.current_sector;
}

std::expected<uint32_t, std::string> CvmVolumeSet::tell(const CvmRofsRangeHandle& handle) const {
    auto validated = validate_range_handle(*this, handle);
    if (!validated) {
        return std::unexpected(validated.error());
    }
    return handle.current_sector;
}

std::expected<CvmRofsTransferStatus, std::string> CvmVolumeSet::status(const CvmRofsRangeHandle& handle) const {
    auto validated = validate_range_handle(*this, handle);
    if (!validated) {
        return std::unexpected(validated.error());
    }
    return handle.last_transfer_status;
}

std::expected<uint64_t, std::string> CvmVolumeSet::transferred_bytes(const CvmRofsRangeHandle& handle) const {
    auto validated = validate_range_handle(*this, handle);
    if (!validated) {
        return std::unexpected(validated.error());
    }
    return static_cast<uint64_t>(handle.last_transfer_sector_count) * rofs_sector_length();
}

std::expected<void, std::string> CvmVolumeSet::close(CvmRofsRangeHandle& handle) const {
    handle = {};
    return {};
}

std::expected<void, std::string> CvmVolumeSet::stop_transfer(CvmRofsRangeHandle& handle) const {
    auto validated = validate_range_handle(*this, handle);
    if (!validated) {
        return std::unexpected(validated.error());
    }

    handle.last_transfer_sector_count = 0;
    handle.last_transfer_status = CvmRofsTransferStatus::idle;
    return {};
}

std::expected<std::vector<uint8_t>, std::string> CvmVolumeSet::read_sectors(
    CvmRofsRangeHandle& handle,
    uint32_t sector_count
) const {
    auto validated = validate_range_handle(*this, handle);
    if (!validated) {
        handle.last_transfer_status = CvmRofsTransferStatus::error;
        handle.last_transfer_sector_count = 0;
        return std::unexpected(validated.error());
    }
    const auto* volume = *validated;

    const uint32_t remaining_sectors = handle.sector_count - handle.current_sector;
    const uint32_t sectors_to_read = std::min(sector_count, remaining_sectors);
    auto sector_data = volume->image.sector_range_data(handle.start_sector + handle.current_sector, sectors_to_read);
    if (!sector_data) {
        handle.last_transfer_status = CvmRofsTransferStatus::error;
        handle.last_transfer_sector_count = 0;
        return std::unexpected(sector_data.error());
    }

    handle.current_sector += sectors_to_read;
    handle.last_transfer_sector_count = sectors_to_read;
    handle.last_transfer_status = CvmRofsTransferStatus::complete;
    return std::vector<uint8_t>(sector_data->begin(), sector_data->end());
}

const CvmMountedVolume* CvmVolumeSet::find_volume(std::string_view volume_name) const noexcept {
    const std::string normalized = normalize_volume_name(volume_name);
    for (const auto& volume : m_volumes) {
        if (volume.name == normalized) {
            return &volume;
        }
    }
    return nullptr;
}

std::expected<CvmRofsVolumeInfo, std::string> CvmVolumeSet::volume_info(std::string_view volume_name) const {
    const auto* volume = find_volume(volume_name);
    if (volume == nullptr) {
        return std::unexpected("CVM volume is not mounted: " + normalize_volume_name(volume_name));
    }

    return CvmRofsVolumeInfo{
        .name = volume->name,
        .source_path = volume->image.source_path(),
        .current_directory = volume->current_directory,
        .is_default = m_default_volume_name.has_value() && *m_default_volume_name == volume->name,
        .is_scrambled = volume->image.is_scrambled(),
    };
}

std::expected<CvmRofsVolumeInfo, std::string> CvmVolumeSet::default_volume_info() const {
    if (!m_default_volume_name.has_value()) {
        return std::unexpected("CVM default volume is not configured");
    }
    return volume_info(*m_default_volume_name);
}

std::expected<CvmRofsScrambleInfo, std::string> CvmVolumeSet::scramble_info(
    const std::filesystem::path& runtime_path
) const {
    const auto resolved = resolve_volume_and_archive_path(*this, runtime_path);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }

    const auto* volume = resolved->first;
    uint32_t sector = 0;

    if (const auto* entry = volume->image.find_entry(resolved->second); entry != nullptr) {
        sector = entry->extent_sector;
    } else {
        auto directory = volume->image.directory_record(resolved->second);
        if (!directory) {
            return std::unexpected("CVM runtime scramble path was not found: " + runtime_path.generic_string());
        }
        sector = directory->extent_sector;
    }

    CvmRofsScrambleInfo info{
        .volume_name = volume->name,
        .volume_token = volume->scramble_handle_token,
        .initial_sector = sector,
        .current_sector = sector,
        .is_scrambled = volume->image.is_scrambled(),
    };
    info.raw_words[0] = static_cast<int32_t>(volume->scramble_handle_token);
    info.raw_words[1] = static_cast<int32_t>(sector);
    return info;
}

std::expected<void, std::string> CvmVolumeSet::descramble(
    std::span<uint8_t> sector_data,
    CvmRofsScrambleInfo& scramble_info
) const {
    if (scramble_info.volume_token == 0) {
        return std::unexpected("CVM ROFS scramble descriptor does not reference a mounted volume");
    }
    if (sector_data.size() % rofs_sector_length() != 0) {
        return std::unexpected(
            "CVM ROFS descramble data size must be a multiple of 2048 bytes: " +
            std::to_string(sector_data.size())
        );
    }

    const auto volume_it = std::ranges::find_if(m_volumes, [&](const CvmMountedVolume& volume) {
        return volume.scramble_handle_token == scramble_info.volume_token;
    });
    if (volume_it == m_volumes.end()) {
        return std::unexpected(
            "CVM ROFS scramble volume token is not mounted: " + std::to_string(scramble_info.volume_token)
        );
    }

    const std::string descriptor_volume_name = normalize_volume_name(scramble_info.volume_name);
    if (!descriptor_volume_name.empty() && descriptor_volume_name != volume_it->name) {
        return std::unexpected(
            "CVM ROFS scramble descriptor volume mismatch: descriptor='" + descriptor_volume_name +
            "', mounted='" + volume_it->name + "'"
        );
    }

    if (volume_it->image.is_scrambled()) {
        return std::unexpected(
            "CVM ROFS descramble is not implemented for scrambled volume '" + volume_it->name + "'"
        );
    }

    const uint32_t sector_count = static_cast<uint32_t>(sector_data.size() / rofs_sector_length());
    advance_scramble_info(scramble_info, sector_count);
    return {};
}

void CvmVolumeSet::advance_scramble_info(CvmRofsScrambleInfo& scramble_info, uint32_t sector_count) noexcept {
    scramble_info.current_sector += sector_count;
    scramble_info.raw_words[1] = static_cast<int32_t>(scramble_info.current_sector);
}

std::optional<std::filesystem::path> CvmVolumeSet::current_directory(std::string_view volume_name) const {
    const auto* volume = find_volume(volume_name);
    if (volume == nullptr) {
        return std::nullopt;
    }
    return volume->current_directory;
}

const CvmEntry* CvmVolumeSet::find_entry(const std::filesystem::path& runtime_path) const noexcept {
    const auto resolved = resolve_volume_and_archive_path(*this, runtime_path);
    if (!resolved) {
        return nullptr;
    }
    return resolved->first->image.find_entry(resolved->second);
}

bool CvmVolumeSet::file_exists(const std::filesystem::path& runtime_path) const noexcept {
    return find_entry(runtime_path) != nullptr;
}

bool CvmVolumeSet::file_exists(
    const std::filesystem::path& relative_path,
    std::span<const uint8_t> rofs_directory_record
) const noexcept {
    const auto context = resolve_rofs_directory_context(*this, rofs_directory_record);
    return context && context->volume->image.find_entry(relative_path, context->directory);
}

std::expected<uint64_t, std::string> CvmVolumeSet::file_size(const std::filesystem::path& runtime_path) const {
    const auto* entry = find_entry(runtime_path);
    if (entry == nullptr) {
        return std::unexpected("CVM runtime file path was not found: " + runtime_path.generic_string());
    }
    return entry->size;
}

std::expected<uint64_t, std::string> CvmVolumeSet::file_size(
    const std::filesystem::path& relative_path,
    std::span<const uint8_t> rofs_directory_record
) const {
    const auto context = resolve_rofs_directory_context(*this, rofs_directory_record);
    if (!context) {
        return std::unexpected(context.error());
    }

    const auto* entry = context->volume->image.find_entry(relative_path, context->directory);
    if (entry == nullptr) {
        return std::unexpected(
            "CVM file path was not found relative to ROFS directory record '" +
            context->directory.directory_path.generic_string() + "': " + relative_path.generic_string()
        );
    }
    return entry->size;
}

std::expected<uint32_t, std::string> CvmVolumeSet::rofs_num_files(
    std::span<const uint8_t> rofs_directory_record
) {
    const auto parsed = parse_rofs_directory_record_header(rofs_directory_record);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return parsed->dir_num;
}

std::expected<uint32_t, std::string> CvmVolumeSet::rofs_num_files(
    const std::filesystem::path& runtime_path
) const {
    return directory_record(runtime_path).transform([](const CvmDirectoryRecord& directory) {
        return static_cast<uint32_t>(directory.entries.size());
    });
}

std::expected<uint32_t, std::string> CvmVolumeSet::rofs_num_files_for_volume(
    std::string_view volume_name
) const {
    return directory_record_for_volume(volume_name).transform([](const CvmDirectoryRecord& directory) {
        return static_cast<uint32_t>(directory.entries.size());
    });
}

std::expected<std::vector<CvmRofsFileInfo>, std::string> CvmVolumeSet::rofs_directory_info(
    std::span<const uint8_t> rofs_directory_record
) {
    return parse_rofs_directory_entries(rofs_directory_record);
}

std::expected<std::vector<CvmRofsFileInfo>, std::string> CvmVolumeSet::rofs_directory_info(
    const std::filesystem::path& runtime_path
) const {
    return directory_record(runtime_path).transform(make_rofs_file_info);
}

std::expected<std::vector<CvmRofsFileInfo>, std::string> CvmVolumeSet::rofs_directory_info_for_volume(
    std::string_view volume_name
) const {
    return directory_record_for_volume(volume_name).transform(make_rofs_file_info);
}

std::expected<std::vector<uint8_t>, std::string> CvmVolumeSet::load_iso_directory_record(
    const std::filesystem::path& runtime_path
) const {
    auto handle = open_file(runtime_path);
    if (!handle) {
        return std::unexpected(handle.error());
    }
    return read_sectors(*handle, handle->sector_count);
}

std::expected<std::vector<uint8_t>, std::string> CvmVolumeSet::load_rofs_directory_record(
    const std::filesystem::path& runtime_path,
    uint32_t max_entries
) const {
    const auto resolved = resolve_volume_and_archive_path(*this, runtime_path);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }

    auto directory = resolved->first->image.directory_record(resolved->second);
    if (!directory) {
        return std::unexpected(directory.error());
    }

    if (directory->entries.size() > max_entries) {
        return std::unexpected(
            "CVM ROFS directory record entry count exceeds requested max entries for '" +
            directory->directory_path.generic_string() + "': " + std::to_string(directory->entries.size()) +
            " > " + std::to_string(max_entries)
        );
    }

    std::vector<uint8_t> buffer(rofs_dirrec_header_size + static_cast<size_t>(max_entries) * rofs_dirrec_entry_size, 0);
    write_le<uint32_t>(buffer.data() + 0, static_cast<uint32_t>(directory->entries.size()));
    write_le<uint32_t>(buffer.data() + 4, max_entries);
    write_le<uint32_t>(buffer.data() + 8, directory->extent_sector);

    const std::string volume_name = resolved->first->name;
    std::copy_n(
        reinterpret_cast<const uint8_t*>(volume_name.data()),
        std::min(volume_name.size(), rofs_dirrec_volume_name_size - 1),
        buffer.begin() + 12
    );

    for (size_t index = 0; index < directory->entries.size(); ++index) {
        const auto& entry = directory->entries[index];
        const size_t entry_offset = rofs_dirrec_header_size + index * rofs_dirrec_entry_size;
        const uint64_t size = entry.size;
        write_le<uint32_t>(buffer.data() + entry_offset + 0, static_cast<uint32_t>(size & 0xFFFFFFFFu));
        write_le<uint32_t>(buffer.data() + entry_offset + 4, static_cast<uint32_t>(size >> 32));

        const auto extent_sector = entry_extent_sector(resolved->first->image, entry);
        if (!extent_sector) {
            return std::unexpected(extent_sector.error());
        }
        write_le<uint32_t>(buffer.data() + entry_offset + 8, *extent_sector);
        buffer[entry_offset + 12] = entry.is_directory ? rofs_attribute_directory : 0u;
        buffer[entry_offset + 13] = 0u;

        const std::string name = entry.name;
        if (name.size() >= rofs_dirrec_file_name_size) {
            return std::unexpected("CVM ROFS directory record name exceeds 31 characters: " + name);
        }
        std::copy_n(
            reinterpret_cast<const uint8_t*>(name.data()),
            name.size(),
            buffer.begin() + static_cast<std::ptrdiff_t>(entry_offset + 14)
        );
    }

    return buffer;
}

std::expected<CvmDirectoryRecord, std::string> CvmVolumeSet::directory_record(const std::filesystem::path& runtime_path) const {
    if (runtime_path.empty()) {
        if (!m_default_volume_name.has_value()) {
            return std::unexpected("CVM directory record requires an explicit mounted volume or a configured default volume");
        }

        const auto* volume = find_volume(*m_default_volume_name);
        if (volume == nullptr) {
            return std::unexpected("CVM default volume is not mounted: " + *m_default_volume_name);
        }
        return volume->image.directory_record(volume->current_directory);
    }

    const auto resolved = resolve_volume_and_archive_path(*this, runtime_path);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    return resolved->first->image.directory_record(resolved->second);
}

std::expected<CvmDirectoryRecord, std::string> CvmVolumeSet::directory_record_for_volume(
    std::string_view volume_name
) const {
    const auto* volume = find_volume(volume_name);
    if (volume == nullptr) {
        return std::unexpected("CVM volume is not mounted: " + normalize_volume_name(volume_name));
    }
    return volume->image.directory_record(volume->current_directory);
}

std::expected<std::span<const uint8_t>, std::string> CvmVolumeSet::file_data(const std::filesystem::path& runtime_path) const {
    const auto resolved = resolve_volume_and_archive_path(*this, runtime_path);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    return resolved->first->image.file_data(resolved->second);
}

std::expected<std::span<const uint8_t>, std::string> CvmVolumeSet::file_data(
    const std::filesystem::path& relative_path,
    std::span<const uint8_t> rofs_directory_record
) const {
    const auto context = resolve_rofs_directory_context(*this, rofs_directory_record);
    if (!context) {
        return std::unexpected(context.error());
    }
    return context->volume->image.file_data(relative_path, context->directory);
}

} // namespace cricodecs::cvm
