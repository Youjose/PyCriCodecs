#pragma once
/**
 * @file hca_crypto.hpp
 * @brief HCA cipher table generation and in-place crypt helpers.
 */

#include "hca_header.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace cricodecs::hca {

namespace detail {

[[nodiscard]] constexpr uint64_t apply_subkey(uint64_t keycode, uint16_t subkey) noexcept {
    if (subkey == 0) {
        return keycode;
    }
    return keycode * (((static_cast<uint64_t>(subkey) << 16u) | static_cast<uint16_t>(~subkey + 2u)));
}

[[nodiscard]] std::expected<void, std::string> encrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint16_t cipher_type,
    uint64_t keycode
);
[[nodiscard]] std::expected<void, std::string> encrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint16_t cipher_type,
    uint64_t keycode,
    uint16_t subkey
);
[[nodiscard]] std::expected<void, std::string> decrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint64_t keycode
);
[[nodiscard]] std::expected<void, std::string> decrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint64_t keycode,
    uint16_t subkey
);

} // namespace detail

namespace cipher {

[[nodiscard]] constexpr std::array<uint8_t, 16> type56_nibble_row(uint8_t key) noexcept {
    const int multiplier = ((key & 1) << 3) | 5;
    const int addend = (key & 0x0E) | 1;
    uint8_t state = key >> 4;
    std::array<uint8_t, 16> row{};
    for (auto& value : row) {
        state = static_cast<uint8_t>((state * multiplier + addend) & 0x0F);
        value = state;
    }
    return row;
}

inline void init_cipher_type0(std::span<uint8_t, 256> table) noexcept {
    for (int i = 0; i < 256; i++) {
        table[i] = static_cast<uint8_t>(i);
    }
}

inline void init_cipher_type1(std::span<uint8_t, 256> table) noexcept {
    uint8_t v = 0;
    for (int i = 1; i < 256 - 1; i++) {
        v = static_cast<uint8_t>(v * 13 + 11);
        if (v == 0 || v == 0xFF) {
            v = static_cast<uint8_t>(v * 13 + 11);
        }
        table[i] = v;
    }
    table[0] = 0x00;
    table[0xFF] = 0xFF;
}

inline void init_cipher_type56(std::span<uint8_t, 256> table, uint64_t keycode) noexcept {
    if (keycode != 0) {
        keycode--;
    }

    std::array<uint8_t, 8> kc{};
    for (int i = 0; i < 7; ++i) {
        kc[i] = static_cast<uint8_t>(keycode & 0xFF);
        keycode >>= 8;
    }

    std::array<uint8_t, 16> seed{};
    seed[0x00] = kc[1];
    seed[0x01] = static_cast<uint8_t>(kc[1] ^ kc[6]);
    seed[0x02] = static_cast<uint8_t>(kc[2] ^ kc[3]);
    seed[0x03] = kc[2];
    seed[0x04] = static_cast<uint8_t>(kc[2] ^ kc[1]);
    seed[0x05] = static_cast<uint8_t>(kc[3] ^ kc[4]);
    seed[0x06] = kc[3];
    seed[0x07] = static_cast<uint8_t>(kc[3] ^ kc[2]);
    seed[0x08] = static_cast<uint8_t>(kc[4] ^ kc[5]);
    seed[0x09] = kc[4];
    seed[0x0A] = static_cast<uint8_t>(kc[4] ^ kc[3]);
    seed[0x0B] = static_cast<uint8_t>(kc[5] ^ kc[6]);
    seed[0x0C] = kc[5];
    seed[0x0D] = static_cast<uint8_t>(kc[5] ^ kc[4]);
    seed[0x0E] = static_cast<uint8_t>(kc[6] ^ kc[1]);
    seed[0x0F] = kc[6];

    std::array<uint8_t, 256> base{};
    std::array<uint8_t, 16> base_r{};
    std::array<uint8_t, 16> base_c{};
    base_r = type56_nibble_row(kc[0]);
    for (int r = 0; r < 16; ++r) {
        base_c = type56_nibble_row(seed[r]);
        const uint8_t high = static_cast<uint8_t>(base_r[r] << 4);
        for (int c = 0; c < 16; ++c) {
            base[r * 16 + c] = static_cast<uint8_t>(high | base_c[c]);
        }
    }

    uint32_t x = 0;
    uint32_t pos = 1;
    for (int i = 0; i < 256; ++i) {
        x = (x + 17) & 0xFF;
        if (base[x] != 0 && base[x] != 0xFF) {
            table[pos++] = base[x];
        }
    }
    table[0x00] = 0x00;
    table[0xFF] = 0xFF;
}

inline void init_cipher(std::span<uint8_t, 256> table, uint16_t cipher_type, uint64_t keycode) noexcept {
    if (cipher_type == 56 && keycode == 0) {
        cipher_type = 0;
    }
    switch (cipher_type) {
        case 0: init_cipher_type0(table); break;
        case 1: init_cipher_type1(table); break;
        case 56: init_cipher_type56(table, keycode); break;
        default: init_cipher_type0(table); break;
    }
}

inline void init_cipher(std::span<uint8_t, 256> table, uint16_t cipher_type, uint64_t keycode, uint16_t subkey) noexcept {
    init_cipher(table, cipher_type, detail::apply_subkey(keycode, subkey));
}

inline void transform_frame(
    const std::span<const uint8_t, 256> table, std::span<uint8_t> frame) noexcept {
    for (size_t i = 2; i + 2 < frame.size(); ++i) {
        frame[i] = table[frame[i]];
    }
}

} // namespace cipher

} // namespace cricodecs::hca
