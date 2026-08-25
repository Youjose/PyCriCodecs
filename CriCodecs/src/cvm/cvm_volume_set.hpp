#pragma once
/**
 * @file cvm_volume_set.hpp
 * @brief Mounted-volume helper for ROFS lookups and synchronous sector reads.
 *
 * Async transfer state is outside this helper's scope.
 */

#include <array>
#include <expected>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cvm_container.hpp"

namespace cricodecs::cvm {

struct CvmMountedVolume {
    std::string name;
    CvmContainer image;
    std::filesystem::path current_directory;
    uint32_t scramble_handle_token = 0;
};

struct CvmRofsFileInfo {
    std::string name;
    uint64_t size = 0;
    bool is_directory = false;
};

struct CvmRofsVolumeInfo {
    std::string name;
    std::filesystem::path source_path;
    std::filesystem::path current_directory;
    bool is_default = false;
    bool is_scrambled = false;
};

struct CvmRofsScrambleInfo {
    std::string volume_name;
    uint32_t volume_token = 0;
    uint32_t initial_sector = 0;
    uint32_t current_sector = 0;
    bool is_scrambled = false;
    std::array<int32_t, 8> raw_words{};
};

enum class CvmRofsTransferStatus {
    idle = 0,
    complete = 1,
    transferring = 2,
    error = 3,
};

enum class CvmRofsSeekMode {
    set = 0,
    current = 1,
    end = 2,
};

struct CvmRofsRangeHandle {
    std::string volume_name;
    uint32_t start_sector = 0;
    uint32_t sector_count = 0;
    uint32_t current_sector = 0;
    uint64_t byte_size = 0;
    uint32_t last_transfer_sector_count = 0;
    CvmRofsTransferStatus last_transfer_status = CvmRofsTransferStatus::idle;
};

class CvmVolumeSet {
public:
    static constexpr uint32_t rofs_sector_length() noexcept { return CvmContainer::sector_length(); }

    [[nodiscard]] size_t volume_count() const noexcept { return m_volumes.size(); }
    [[nodiscard]] const std::vector<CvmMountedVolume>& volumes() const noexcept { return m_volumes; }
    [[nodiscard]] std::optional<std::string_view> default_volume_name() const noexcept;
    [[nodiscard]] std::expected<std::string, std::string> default_volume() const;

    [[nodiscard]] std::expected<void, std::string> mount(std::string_view volume_name, CvmContainer&& image);
    [[nodiscard]] std::expected<void, std::string> switch_image(std::string_view volume_name, CvmContainer&& image);
    [[nodiscard]] std::expected<void, std::string> unmount(std::string_view volume_name);
    [[nodiscard]] std::expected<void, std::string> set_default_volume(std::string_view volume_name);
    [[nodiscard]] std::expected<void, std::string> change_directory(const std::filesystem::path& runtime_path) {
        return set_current_directory(runtime_path);
    }
    [[nodiscard]] std::expected<void, std::string> set_current_directory(const std::filesystem::path& runtime_path);
    [[nodiscard]] std::expected<void, std::string> set_current_directory(std::span<const uint8_t> rofs_directory_record);
    [[nodiscard]] std::expected<void, std::string> set_current_directory_iso(
        std::string_view volume_name,
        std::span<const uint8_t> iso_directory_record
    );
    [[nodiscard]] std::expected<void, std::string> set_current_directory_iso(
        std::string_view volume_name,
        std::span<const uint8_t> iso_directory_record,
        uint32_t iso_directory_sector_count
    );
    [[nodiscard]] std::expected<void, std::string> set_current_directory_iso(
        std::string_view volume_name,
        std::span<uint8_t> iso_directory_record,
        CvmRofsScrambleInfo& scramble_info
    );
    [[nodiscard]] std::expected<void, std::string> set_current_directory_iso(
        std::string_view volume_name,
        std::span<uint8_t> iso_directory_record,
        uint32_t iso_directory_sector_count,
        CvmRofsScrambleInfo& scramble_info
    );
    [[nodiscard]] std::expected<CvmRofsRangeHandle, std::string> open_range(
        std::string_view volume_name,
        uint32_t start_sector,
        uint32_t sector_count
    ) const;
    [[nodiscard]] std::expected<CvmRofsRangeHandle, std::string> open_file(
        const std::filesystem::path& runtime_path
    ) const;
    [[nodiscard]] std::expected<CvmRofsRangeHandle, std::string> open_file(
        const std::filesystem::path& relative_path,
        std::span<const uint8_t> rofs_directory_record
    ) const;
    [[nodiscard]] std::expected<uint32_t, std::string> seek(
        CvmRofsRangeHandle& handle,
        int32_t sector_offset,
        CvmRofsSeekMode seek_mode
    ) const;
    [[nodiscard]] std::expected<uint32_t, std::string> tell(const CvmRofsRangeHandle& handle) const;
    [[nodiscard]] std::expected<CvmRofsTransferStatus, std::string> status(const CvmRofsRangeHandle& handle) const;
    [[nodiscard]] std::expected<uint64_t, std::string> transferred_bytes(const CvmRofsRangeHandle& handle) const;
    [[nodiscard]] std::expected<uint64_t, std::string> transferred_bytes64(const CvmRofsRangeHandle& handle) const {
        return transferred_bytes(handle);
    }
    [[nodiscard]] std::expected<void, std::string> close(CvmRofsRangeHandle& handle) const;
    [[nodiscard]] std::expected<void, std::string> stop_transfer(CvmRofsRangeHandle& handle) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> read_sectors(
        CvmRofsRangeHandle& handle,
        uint32_t sector_count
    ) const;

    [[nodiscard]] const CvmMountedVolume* find_volume(std::string_view volume_name) const noexcept;
    [[nodiscard]] std::expected<CvmRofsVolumeInfo, std::string> volume_info(std::string_view volume_name) const;
    [[nodiscard]] std::expected<CvmRofsVolumeInfo, std::string> default_volume_info() const;
    [[nodiscard]] std::expected<CvmRofsScrambleInfo, std::string> scramble_info(
        const std::filesystem::path& runtime_path
    ) const;
    [[nodiscard]] std::expected<void, std::string> descramble(
        std::span<uint8_t> sector_data,
        CvmRofsScrambleInfo& scramble_info
    ) const;
    static void advance_scramble_info(CvmRofsScrambleInfo& scramble_info, uint32_t sector_count) noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> current_directory(std::string_view volume_name) const;
    [[nodiscard]] const CvmEntry* find_entry(const std::filesystem::path& runtime_path) const noexcept;
    [[nodiscard]] bool file_exists(const std::filesystem::path& runtime_path) const noexcept;
    [[nodiscard]] bool file_exists(
        const std::filesystem::path& relative_path,
        std::span<const uint8_t> rofs_directory_record
    ) const noexcept;
    [[nodiscard]] std::expected<uint64_t, std::string> file_size(const std::filesystem::path& runtime_path) const;
    [[nodiscard]] std::expected<uint64_t, std::string> file_size64(const std::filesystem::path& runtime_path) const {
        return file_size(runtime_path);
    }
    [[nodiscard]] std::expected<uint64_t, std::string> file_size(
        const std::filesystem::path& relative_path,
        std::span<const uint8_t> rofs_directory_record
    ) const;
    [[nodiscard]] std::expected<uint64_t, std::string> file_size64(
        const std::filesystem::path& relative_path,
        std::span<const uint8_t> rofs_directory_record
    ) const {
        return file_size(relative_path, rofs_directory_record);
    }
    [[nodiscard]] static std::expected<uint32_t, std::string> rofs_num_files(
        std::span<const uint8_t> rofs_directory_record
    );
    [[nodiscard]] std::expected<uint32_t, std::string> rofs_num_files(
        const std::filesystem::path& runtime_path = {}
    ) const;
    [[nodiscard]] std::expected<uint32_t, std::string> rofs_num_files_for_volume(
        std::string_view volume_name
    ) const;
    [[nodiscard]] static std::expected<std::vector<CvmRofsFileInfo>, std::string> rofs_directory_info(
        std::span<const uint8_t> rofs_directory_record
    );
    [[nodiscard]] std::expected<std::vector<CvmRofsFileInfo>, std::string> rofs_directory_info(
        const std::filesystem::path& runtime_path = {}
    ) const;
    [[nodiscard]] std::expected<std::vector<CvmRofsFileInfo>, std::string> rofs_directory_info_for_volume(
        std::string_view volume_name
    ) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> load_iso_directory_record(
        const std::filesystem::path& runtime_path
    ) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> load_rofs_directory_record(
        const std::filesystem::path& runtime_path,
        uint32_t max_entries
    ) const;
    [[nodiscard]] std::expected<CvmDirectoryRecord, std::string> directory_record(
        const std::filesystem::path& runtime_path = {}
    ) const;
    [[nodiscard]] std::expected<CvmDirectoryRecord, std::string> directory_record_for_volume(
        std::string_view volume_name
    ) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data(const std::filesystem::path& runtime_path) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data(
        const std::filesystem::path& relative_path,
        std::span<const uint8_t> rofs_directory_record
    ) const;

private:
    std::vector<CvmMountedVolume> m_volumes;
    std::optional<std::string> m_default_volume_name;
    uint32_t m_next_scramble_handle_token = 1;
};

} // namespace cricodecs::cvm
