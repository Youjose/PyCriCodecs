#pragma once

#include "simd.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace cricodecs::simd {

inline constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

[[nodiscard]] inline std::size_t find_zero_zero_one(
    std::span<const std::uint8_t> bytes,
    std::size_t offset = 0) noexcept {
    const bytes32 zero{std::uint8_t{0}};
    const bytes32 one{std::uint8_t{1}};

    while (offset <= bytes.size() && bytes.size() - offset >= bytes32::size() + 2) {
        const auto matches =
            (load<bytes32>(bytes.data() + offset) == zero) &
            (load<bytes32>(bytes.data() + offset + 1) == zero) &
            (load<bytes32>(bytes.data() + offset + 2) == one);
        if (std::simd::any_of(matches)) {
            return offset + std::simd::reduce_min_index(matches);
        }
        offset += bytes32::size();
    }

    while (offset <= bytes.size() && bytes.size() - offset >= 3) {
        const auto* zero_byte = static_cast<const std::uint8_t*>(
            std::memchr(bytes.data() + offset, 0, bytes.size() - offset - 2));
        if (zero_byte == nullptr) {
            return npos;
        }
        offset = static_cast<std::size_t>(zero_byte - bytes.data());
        if (bytes[offset + 1] == 0 && bytes[offset + 2] == 1) {
            return offset;
        }
        ++offset;
    }
    return npos;
}

} // namespace cricodecs::simd
