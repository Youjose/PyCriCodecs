#pragma once
/**
 * @file usm_crypto.hpp
 * @brief USM stream masking API.
 *
 * The mask algorithm was ported from PyCriCodecsEx.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace cricodecs::usm {

namespace detail {

using UsmMask = std::array<uint8_t, 0x20>;

[[nodiscard]] constexpr std::array<uint8_t, 7> seed_from_key(uint64_t key) noexcept {
    const uint32_t lower = static_cast<uint32_t>(key);
    const uint32_t upper = static_cast<uint32_t>(key >> 32u);
    return {
        static_cast<uint8_t>(lower),
        static_cast<uint8_t>(lower >> 8u),
        static_cast<uint8_t>(lower >> 16u),
        static_cast<uint8_t>((lower >> 24u) - 0x34u),
        static_cast<uint8_t>(upper + 0xF9u),
        static_cast<uint8_t>((upper >> 8u) ^ 0x13u),
        static_cast<uint8_t>((upper >> 16u) + 0x61u),
    };
}

[[nodiscard]] constexpr UsmMask expand_video_mask(const std::array<uint8_t, 7>& seed) noexcept {
    const auto add = [](uint8_t left, uint8_t right) { return static_cast<uint8_t>(left + right); };
    const auto subtract = [](uint8_t left, uint8_t right) { return static_cast<uint8_t>(left - right); };

    UsmMask mask{};
    std::ranges::copy(seed, mask.begin());
    mask[0x07] = static_cast<uint8_t>(mask[0x00] ^ 0xFFu);
    mask[0x08] = add(mask[0x02], mask[0x01]);
    mask[0x09] = subtract(mask[0x01], mask[0x07]);
    mask[0x0A] = static_cast<uint8_t>(mask[0x02] ^ 0xFFu);
    mask[0x0B] = static_cast<uint8_t>(mask[0x01] ^ 0xFFu);
    mask[0x0C] = add(mask[0x0B], mask[0x09]);
    mask[0x0D] = subtract(mask[0x08], mask[0x03]);
    mask[0x0E] = static_cast<uint8_t>(mask[0x0D] ^ 0xFFu);
    mask[0x0F] = subtract(mask[0x0A], mask[0x0B]);
    mask[0x10] = subtract(mask[0x08], mask[0x0F]);
    mask[0x11] = static_cast<uint8_t>(mask[0x10] ^ mask[0x07]);
    mask[0x12] = static_cast<uint8_t>(mask[0x0F] ^ 0xFFu);
    mask[0x13] = static_cast<uint8_t>(mask[0x03] ^ 0x10u);
    mask[0x14] = subtract(mask[0x04], 0x32u);
    mask[0x15] = add(mask[0x05], 0xEDu);
    mask[0x16] = static_cast<uint8_t>(mask[0x06] ^ 0xF3u);
    mask[0x17] = subtract(mask[0x13], mask[0x0F]);
    mask[0x18] = add(mask[0x15], mask[0x07]);
    mask[0x19] = subtract(0x21u, mask[0x13]);
    mask[0x1A] = static_cast<uint8_t>(mask[0x14] ^ mask[0x17]);
    mask[0x1B] = add(mask[0x16], mask[0x16]);
    mask[0x1C] = add(mask[0x17], 0x44u);
    mask[0x1D] = add(mask[0x03], mask[0x04]);
    mask[0x1E] = subtract(mask[0x05], mask[0x16]);
    mask[0x1F] = static_cast<uint8_t>(mask[0x1D] ^ mask[0x13]);
    return mask;
}

} // namespace detail

class UsmCrypto {
public:
    void init_key(uint64_t key);
    void clear_key() noexcept;

    void decrypt_video(std::span<uint8_t> data) const;
    void encrypt_video(std::span<uint8_t> data) const;
    void decrypt_audio(std::span<uint8_t> data) const;
    void encrypt_audio(std::span<uint8_t> data) const {
        decrypt_audio(data);
    }

    /// Return the initialized 32-byte USM audio mask.
    [[nodiscard]] const std::array<uint8_t, 0x20>& audio_mask() const noexcept {
        return m_audio_mask;
    }
    /// Invert the key-dependent even bytes of a complete USM audio mask.
    [[nodiscard]] static uint64_t recover_key_from_audio_mask(
        std::span<const uint8_t, 0x20> mask
    ) noexcept;

    [[nodiscard]] bool has_key() const noexcept { return m_has_key; }

private:
    bool m_has_key = false;
    std::array<uint8_t, 0x20> m_video_mask1{};
    std::array<uint8_t, 0x20> m_video_mask2{};
    std::array<uint8_t, 0x20> m_audio_mask{};
};

} // namespace cricodecs::usm
