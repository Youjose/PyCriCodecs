/**
 * @file cvm_key_recovery.cpp
 * @brief Exact recovery of the effective CVM TOC scramble key.
 *
 * The recovery observation and implementation are CriCodecs work by Youjose:
 * an encrypted CVM exposes one complete local-key block through the fixed
 * eight-byte ISO9660 primary-volume descriptor prefix.
 */

#include "cvm_key_recovery.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include "cvm_crypto.hpp"
#include "../utilities/io.hpp"
#include "../utilities/io_endian.hpp"

namespace cricodecs::cvm {

namespace {

constexpr uint32_t sector_size = 0x800u;
constexpr uint32_t pvd_sector = 16;
constexpr size_t zone_offset = sector_size;
constexpr std::array<uint8_t, 4> cvmh_magic = {'C', 'V', 'M', 'H'};
constexpr std::array<uint8_t, 4> zone_magic = {'Z', 'O', 'N', 'E'};
constexpr std::array<uint8_t, 8> pvd_prefix = {0x01, 'C', 'D', '0', '0', '1', 0x01, 0x00};

[[nodiscard]] bool has_magic(std::span<const uint8_t> data, size_t offset, std::span<const uint8_t> magic) {
    return offset <= data.size() && magic.size() <= data.size() - offset &&
           std::equal(magic.begin(), magic.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] bool valid_primary_volume(std::span<const uint8_t, sector_size> sector) noexcept {
    if (!std::equal(pvd_prefix.begin(), pvd_prefix.end(), sector.begin())) {
        return false;
    }

    const uint32_t volume_sectors_le = io::read_le<uint32_t>(sector.data() + 80);
    const uint32_t volume_sectors_be = io::read_be<uint32_t>(sector.data() + 84);
    const uint16_t volume_set_size_le = io::read_le<uint16_t>(sector.data() + 120);
    const uint16_t volume_set_size_be = io::read_be<uint16_t>(sector.data() + 122);
    const uint16_t volume_sequence_le = io::read_le<uint16_t>(sector.data() + 124);
    const uint16_t volume_sequence_be = io::read_be<uint16_t>(sector.data() + 126);
    const uint16_t block_size_le = io::read_le<uint16_t>(sector.data() + 128);
    const uint16_t block_size_be = io::read_be<uint16_t>(sector.data() + 130);
    const uint32_t path_table_size_le = io::read_le<uint32_t>(sector.data() + 132);
    const uint32_t path_table_size_be = io::read_be<uint32_t>(sector.data() + 136);

    return volume_sectors_le == volume_sectors_be && volume_sectors_le > pvd_sector &&
           volume_set_size_le == volume_set_size_be && volume_set_size_le != 0 &&
           volume_sequence_le == volume_sequence_be && volume_sequence_le != 0 &&
           block_size_le == block_size_be && block_size_le == sector_size &&
           path_table_size_le == path_table_size_be && path_table_size_le != 0 &&
           sector[156] >= 34 && (sector[181] & 0x02u) != 0;
}

} // namespace

std::expected<CvmKey, std::string> recover_key(std::span<const uint8_t> data) {
    if (data.size() < zone_offset + sector_size) {
        return std::unexpected("CVM key recovery source is too small");
    }
    if (!has_magic(data, 0, cvmh_magic) || !has_magic(data, zone_offset, zone_magic)) {
        return std::unexpected("CVM key recovery source has invalid CVMH or ZONE magic");
    }
    if ((io::read_be<uint32_t>(data.data() + 0x30) & 0x10u) == 0) {
        return std::unexpected("CVM key recovery source is not scrambled");
    }

    const uint32_t iso_start_sector = io::read_be<uint32_t>(data.data() + 0x88);
    if (iso_start_sector != io::read_be<uint32_t>(data.data() + zone_offset + 0x2C)) {
        return std::unexpected("CVM key recovery ISO sector disagrees between CVMH and ZONE");
    }
    const size_t iso_start = iso_start_sector;
    if (iso_start > std::numeric_limits<size_t>::max() / sector_size - pvd_sector) {
        return std::unexpected("CVM key recovery primary-volume offset overflows");
    }

    const size_t pvd_offset = (iso_start + pvd_sector) * sector_size;
    if (pvd_offset > data.size() || sector_size > data.size() - pvd_offset) {
        return std::unexpected("CVM key recovery primary-volume sector is out of bounds");
    }

    std::array<uint8_t, 8> first_local_key{};
    for (size_t index = 0; index < first_local_key.size(); ++index) {
        first_local_key[index] = data[pvd_offset + index] ^ pvd_prefix[index];
    }

    for (uint32_t key_byte_5 = 0; key_byte_5 <= 0xFFu; ++key_byte_5) {
        const auto hash = crypto::detail::calc_hash(pvd_sector * key_byte_5);
        CvmKey candidate{};
        crypto::detail::invert_scramble(
            first_local_key,
            hash.hash,
            candidate,
            crypto::detail::scrambles[hash.scramble_index]
        );
        if (candidate[5] != key_byte_5) {
            continue;
        }

        std::array<uint8_t, sector_size> primary_volume{};
        std::copy_n(data.data() + static_cast<std::ptrdiff_t>(pvd_offset), sector_size, primary_volume.begin());
        crypto::transform_sectors(primary_volume, pvd_sector, 1, sector_size, candidate);
        if (!valid_primary_volume(primary_volume)) {
            continue;
        }

        return candidate;
    }

    return std::unexpected("CVM key recovery found no key that decrypts the ISO TOC");
}

std::expected<CvmKey, std::string> recover_key(const std::filesystem::path& path) {
    return io::read_file_bytes(path, "CVM key recovery failed").and_then(
        [](const auto& data) { return recover_key(data); });
}

} // namespace cricodecs::cvm
