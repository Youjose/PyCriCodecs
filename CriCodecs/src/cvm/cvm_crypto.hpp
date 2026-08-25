#pragma once
/**
 * @file cvm_crypto.hpp
 * @brief CVM/ROFS TOC scramble helpers.
 *
 * Key and mask implementation by Youjose, based on the CRI ROFS algorithm.
 */

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "../utilities/io_endian.hpp"
#include "../utilities/primes.hpp"

namespace cricodecs::cvm::crypto {

namespace detail {

inline constexpr const auto& primes = cricodecs::util::cri_key_primes;

constexpr std::array<const char*, 9> scrambles = {
    "^03 .0 37 .4 .1 26 .2 15",
    "^12 .7 .5 23 00 .6 .4 31",
    "^.1 27 .6 12 35 .3 00 .4",
    "+23 .6 .0 .2 04 11 .7 35",
    "+.7 30 02 16 .4 .3 .5 21",
    "+.2 23 .6 07 .0 11 .4 35",
    "+03 .7^12 .6 .1 25 .0+34",
    " .7^34 .3+21 .0 .2 15^06",
    " .3^10 .6+04^32 .7 .1+25",
};

inline uint16_t calc_one_val(std::span<const uint8_t> bytes, uint16_t start_val) noexcept {
    uint16_t value = start_val;
    for (const uint8_t byte : bytes) {
        const auto signed_byte = static_cast<int8_t>(byte);
        const int prime_index = 128 + static_cast<int>(signed_byte);
        const int p = primes[prime_index] * value;
        value = static_cast<uint16_t>(primes[p & 0x3FF]);
    }
    return value;
}

struct HashVals {
    uint16_t value1 = 0;
    uint16_t value2 = 0;
    uint16_t value3 = 0;
};

struct HashResult {
    std::array<uint8_t, 4> hash{};
    uint32_t scramble_index = 0;
};

inline HashVals calc_hash_vals(std::span<const uint8_t> bytes) noexcept {
    return {
        .value1 = calc_one_val(bytes, 18973),
        .value2 = calc_one_val(bytes, 21503),
        .value3 = calc_one_val(bytes, 24001),
    };
}

inline HashResult calc_hash(uint32_t seed) noexcept {
    std::array<uint8_t, 4> buffer{};
    io::write_be<uint32_t>(buffer.data(), 0x00100001u * seed);

    const HashVals values = calc_hash_vals(buffer);
    HashResult result{
        .scramble_index = values.value1 % 9u,
    };
    io::write_be<uint16_t>(result.hash.data() + 0, values.value2);
    io::write_be<uint16_t>(result.hash.data() + 2, values.value3);
    return result;
}

inline void apply_scramble(
    std::span<const uint8_t, 8> source,
    std::span<const uint8_t, 4> hash,
    std::span<uint8_t, 8> destination,
    const char* scramble
) noexcept {
    int position = 0;
    char mode = '^';
    for (int i = 0; i < 8; ++i) {
        while (scramble[position] == ' ') {
            ++position;
        }
        if (scramble[position] == '^' || scramble[position] == '+') {
            mode = scramble[position++];
        }

        const char hash_offset = scramble[position++];
        const char source_offset = scramble[position++];
        uint8_t value = source[static_cast<size_t>(source_offset - '0')];
        if (hash_offset != '.') {
            const uint8_t hash_value = hash[static_cast<size_t>(hash_offset - '0')];
            value = mode == '^'
                ? static_cast<uint8_t>(value ^ hash_value)
                : static_cast<uint8_t>(value + hash_value);
        }
        destination[static_cast<size_t>(i)] = value;
    }
}

inline void invert_scramble(
    std::span<const uint8_t, 8> source,
    std::span<const uint8_t, 4> hash,
    std::span<uint8_t, 8> destination,
    const char* scramble
) noexcept {
    int position = 0;
    char mode = '^';
    for (int i = 0; i < 8; ++i) {
        while (scramble[position] == ' ') {
            ++position;
        }
        if (scramble[position] == '^' || scramble[position] == '+') {
            mode = scramble[position++];
        }

        const char hash_offset = scramble[position++];
        const char destination_offset = scramble[position++];
        uint8_t value = source[static_cast<size_t>(i)];
        if (hash_offset != '.') {
            const uint8_t hash_value = hash[static_cast<size_t>(hash_offset - '0')];
            value = mode == '^'
                ? static_cast<uint8_t>(value ^ hash_value)
                : static_cast<uint8_t>(value - hash_value);
        }
        destination[static_cast<size_t>(destination_offset - '0')] = value;
    }
}

inline void calc_local_key(
    std::span<const uint8_t, 8> key,
    std::span<const uint8_t, 4> hash,
    uint32_t scramble_index,
    std::span<uint8_t, 8> local_key
) noexcept {
    apply_scramble(key, hash, local_key, scrambles[scramble_index]);
}

inline void extra_hash(std::span<uint8_t, 8> key) noexcept {
    for (size_t block = 0; block < 4; ++block) {
        const HashVals values = calc_hash_vals(key.subspan(block * 2, 2));
        io::write_be<uint16_t>(key.data() + block * 2, values.value1);
    }
}

} // namespace detail

inline std::array<uint8_t, 8> calc_key_from_string(std::string_view password) noexcept {
    std::array<uint8_t, 8> key{};
    std::array<uint8_t, 4> tmp{};

    uint32_t suffix = 0;
    for (const unsigned char byte : password) {
        suffix += byte;
    }

    uint32_t sum = 0;
    for (const unsigned char current : password) {
        suffix -= current;
        sum = current * (current + sum) + suffix;
    }

    io::write_be<uint32_t>(tmp.data(), 0x00100001u * sum);
    for (size_t index = 0; index < 4; ++index) {
        key[index * 2] = tmp[index];
        key[index * 2 + 1] = tmp[3 - index];
    }
    detail::extra_hash(key);
    return key;
}

inline void transform_sectors(
    std::span<uint8_t> data,
    uint32_t start_sector,
    uint32_t sector_count,
    uint32_t sector_size,
    std::span<const uint8_t, 8> key
) noexcept {
    if (sector_count == 0 || sector_size == 0) {
        return;
    }

    for (uint32_t sector = 0; sector < sector_count; ++sector) {
        uint8_t* sector_bytes = data.data() + static_cast<size_t>(sector) * sector_size;
        uint32_t seed = key[5];
        constexpr uint32_t key_stride = 8;
        for (uint32_t offset = 0; offset < sector_size; offset += key_stride) {
            const auto hash = detail::calc_hash((start_sector + sector) * seed);

            std::array<uint8_t, 8> local_key{};
            detail::calc_local_key(key, hash.hash, hash.scramble_index, local_key);

            seed = hash.scramble_index + offset;
            for (size_t key_index = 0; key_index < key.size() && offset + key_index < sector_size; ++key_index) {
                sector_bytes[offset + key_index] ^= local_key[key_index];
                seed *= local_key[key_index];
            }
        }
    }
}

} // namespace cricodecs::cvm::crypto
