#pragma once
/**
 * @file awb_aac_encryption.hpp
 * @brief CRI AWB AAC encryption and decryption helpers.
 *
 * Ported from vgmstream's awb_aac_encryption_streamfile.h and cross-checked
 * against cri_ware_unity's _criAacCodec_SetDecryptionKey,
 * _criAacCodec_DecryptData, and _criAacCodec_CheckDecryption.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "../utilities/simd.hpp"

namespace cricodecs::awb {

enum class AacEncryptionState {
    Clear,
    Encrypted,
    Indeterminate,
};

[[nodiscard]] inline std::string_view to_string(AacEncryptionState state) noexcept {
    switch (state) {
        case AacEncryptionState::Clear:
            return "clear";
        case AacEncryptionState::Encrypted:
            return "encrypted";
        case AacEncryptionState::Indeterminate:
            return "indeterminate";
        default:
            return "unknown";
    }
}

namespace detail {

struct Affine16 {
    uint16_t mul = 1;
    uint16_t add = 0;
};

[[nodiscard]] constexpr Affine16 compose(Affine16 outer, Affine16 inner) noexcept {
    return {
        static_cast<uint16_t>(static_cast<uint32_t>(outer.mul) * inner.mul),
        static_cast<uint16_t>(static_cast<uint32_t>(outer.mul) * inner.add + outer.add),
    };
}

[[nodiscard]] inline std::array<uint16_t, 4> derive_aac_key(uint64_t keycode) noexcept {
    if (keycode == 0) {
        return {};
    }

    const uint16_t k0 = static_cast<uint16_t>((4u * (keycode & 0x0FFFu)) | 1u);
    const uint16_t k1 = static_cast<uint16_t>((2u * ((keycode >> 12u) & 0x1FFFu)) | 1u);
    const uint16_t k2 = static_cast<uint16_t>((4u * ((keycode >> 25u) & 0x1FFFu)) | 1u);
    const uint16_t k3 = static_cast<uint16_t>((2u * ((keycode >> 38u) & 0x3FFFu)) | 1u);

    return {
        static_cast<uint16_t>(k0 ^ k1),
        static_cast<uint16_t>(k1 ^ k2),
        static_cast<uint16_t>(k2 ^ k3),
        static_cast<uint16_t>(~k3),
    };
}

inline void apply_aac_segment(
    std::span<uint8_t> data, uint16_t& value, uint16_t mul, uint16_t add) noexcept {
    if (data.size() < static_cast<size_t>(simd::words16::size())) {
        for (uint8_t& byte : data) {
            value = static_cast<uint16_t>(static_cast<uint32_t>(value) * mul + add);
            byte ^= static_cast<uint8_t>(value >> 8u);
        }
        return;
    }

    std::array<uint16_t, simd::words16::size()> lane_mul{};
    std::array<uint16_t, simd::words16::size()> lane_add{};
    Affine16 power{};
    for (size_t lane = 0; lane < lane_mul.size(); ++lane) {
        power = compose({mul, add}, power);
        lane_mul[lane] = power.mul;
        lane_add[lane] = power.add;
    }

    const auto multipliers = simd::load<simd::words16>(lane_mul.data());
    const auto addends = simd::load<simd::words16>(lane_add.data());
    std::array<uint16_t, simd::words16::size()> values{};
    size_t offset = 0;
    for (; offset + values.size() <= data.size(); offset += values.size()) {
        simd::store(simd::words16{value} * multipliers + addends, values.data());
        for (size_t lane = 0; lane < values.size(); ++lane) {
            data[offset + lane] ^= static_cast<uint8_t>(values[lane] >> 8u);
        }
        value = values.back();
    }
    for (; offset < data.size(); ++offset) {
        value = static_cast<uint16_t>(static_cast<uint32_t>(value) * mul + add);
        data[offset] ^= static_cast<uint8_t>(value >> 8u);
    }
}

} // namespace detail

inline void apply_aac_keystream(std::span<uint8_t> data, uint64_t keycode) noexcept {
    if (keycode == 0 || data.empty()) {
        return;
    }

    const auto key = detail::derive_aac_key(keycode);
    const uint16_t seed0 = static_cast<uint16_t>(~key[3]);
    const uint16_t seed1 = static_cast<uint16_t>(seed0 ^ key[2]);
    const uint16_t seed2 = static_cast<uint16_t>(seed1 ^ key[1]);
    const uint16_t seed3 = static_cast<uint16_t>(seed2 ^ key[0]);

    uint16_t xor_value = static_cast<uint16_t>((2u * seed0) | 1u);
    uint16_t add_value = xor_value;
    uint16_t mul_value = static_cast<uint16_t>((4u * seed2) | 1u);

    for (size_t offset = 0; offset < data.size();) {
        const uint32_t next_mul = (4u * seed2) + (seed3 * (mul_value & 0xFFFCu));
        const uint32_t next_add = (2u * seed0) + (seed1 * (add_value & 0xFFFEu));
        mul_value = static_cast<uint16_t>((next_mul & 0xFFFDu) | 1u);
        add_value = static_cast<uint16_t>(next_add | 1u);

        const size_t count = std::min(data.size() - offset, size_t{0x10000});
        detail::apply_aac_segment(data.subspan(offset, count), xor_value, mul_value, add_value);
        offset += count;
    }
}

[[nodiscard]] inline std::vector<uint8_t> decrypt_aac(std::span<const uint8_t> data, uint64_t keycode) {
    std::vector<uint8_t> output(data.begin(), data.end());
    apply_aac_keystream(output, keycode);
    return output;
}

/// Encrypt a clear AAC/M4A payload for insertion into an AWB. The CRI stream
/// transform is symmetric, so this intentionally uses the same keystream path
/// as decryption.
[[nodiscard]] inline std::vector<uint8_t> encrypt_aac(std::span<const uint8_t> data, uint64_t keycode) {
    return decrypt_aac(data, keycode);
}

[[nodiscard]] inline AacEncryptionState probe_aac_encryption(std::span<const uint8_t> data,
                                                             uint64_t keycode) {
    constexpr std::array<uint8_t, 4> ftyp_magic{'f', 't', 'y', 'p'};

    if (data.size() < 8) {
        return AacEncryptionState::Indeterminate;
    }

    if (std::equal(ftyp_magic.begin(), ftyp_magic.end(), data.begin() + 4)) {
        return AacEncryptionState::Clear;
    }

    if (keycode == 0) {
        return AacEncryptionState::Indeterminate;
    }

    std::array<uint8_t, 8> probe{};
    std::copy_n(data.begin(), probe.size(), probe.begin());
    apply_aac_keystream(probe, keycode);

    if (std::equal(ftyp_magic.begin(), ftyp_magic.end(), probe.begin() + 4)) {
        return AacEncryptionState::Encrypted;
    }

    return AacEncryptionState::Indeterminate;
}

} // namespace cricodecs::awb
