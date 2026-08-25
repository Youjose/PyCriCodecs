#pragma once
/**
 * @file adx_crypto.hpp
 * @brief ADX/AHX key derivation helpers.
 *
 * Type-8 and type-9 derivation follows the ADX-family behavior cross-checked
 * against VGAudio/vgmstream references and CRI tooling evidence.
 */

#include <cstdint>
#include <string_view>

#include "../utilities/primes.hpp"

namespace cricodecs::adx {

    inline constexpr const auto& KEY8_PRIMES = cricodecs::util::cri_key_primes;

    struct AdxKeyState {
        uint16_t xor_value = 0;
        uint16_t mult = 0;
        uint16_t add = 0;

        constexpr void advance() noexcept {
            xor_value = static_cast<uint16_t>((xor_value * mult + add) & 0x7FFF);
        }

        friend bool operator==(const AdxKeyState&, const AdxKeyState&) = default;
    };

    inline AdxKeyState key8_derive(std::string_view key_string) {
        if (key_string.empty()) {
            return {};
        }

        uint16_t start = KEY8_PRIMES[0x100];
        uint16_t mult = KEY8_PRIMES[0x200];
        uint16_t add = KEY8_PRIMES[0x300];

        for (const unsigned char c : key_string) {
            const uint32_t p = KEY8_PRIMES[c + 0x80];
            
            start = KEY8_PRIMES[(start * p) % 0x400];
            mult = KEY8_PRIMES[(mult * p) % 0x400];
            add = KEY8_PRIMES[(add * p) % 0x400];
        }

        return {
            .xor_value = start,
            .mult = mult,
            .add = add,
        };
    }

    inline AdxKeyState key9_derive(uint64_t key, uint16_t subkey) {
        if (key == 0) {
            return {};
        }

        uint64_t packed = key;
        if (subkey != 0) {
            const uint64_t factor =
                (static_cast<uint64_t>(subkey) << 16u) |
                (static_cast<uint16_t>(~subkey + 2u));
            packed *= factor;
        }
        --packed;
        return {
            .xor_value = static_cast<uint16_t>((packed >> 27u) & 0x7FFFu),
            .mult = static_cast<uint16_t>(((packed >> 12u) & 0x7FFCu) | 1u),
            .add = static_cast<uint16_t>(((packed << 1u) & 0x7FFFu) | 1u),
        };
    }

    /// Return the canonical type-9 keycode for an effective LCG triplet.
    /// Bits which type-9 does not expose are intentionally zeroed.
    inline uint64_t key9_canonical_code(AdxKeyState state) noexcept {
        const uint64_t packed =
            (static_cast<uint64_t>(state.xor_value & 0x1FFFu) << 27u) |
            (static_cast<uint64_t>(state.mult & 0x1FFCu) << 12u) |
            (static_cast<uint64_t>(state.add & 0x1FFFu) >> 1u);
        return packed + 1u;
    }

} // namespace cricodecs::adx
