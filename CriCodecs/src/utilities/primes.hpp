#pragma once
/**
 * @file primes.hpp
 * @brief Compile-time prime table generation helpers.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cricodecs::util {

template <uint32_t First, uint32_t LastExclusive>
consteval auto make_prime_segment_data() {
    static_assert(First < LastExclusive, "prime range must be non-empty");

    constexpr size_t size = LastExclusive - First;
    struct Segment {
        std::array<bool, size> composite;
        size_t prime_count;
    };

    Segment data{};

    for (uint32_t value = First; value < LastExclusive && value < 2; ++value) {
        data.composite[value - First] = true;
    }

    for (uint32_t divisor = 2; static_cast<uint64_t>(divisor) * divisor < LastExclusive; ++divisor) {
        bool divisor_is_prime = true;
        for (uint32_t test = 2; static_cast<uint64_t>(test) * test <= divisor; ++test) {
            if ((divisor % test) == 0) {
                divisor_is_prime = false;
                break;
            }
        }
        if (!divisor_is_prime) {
            continue;
        }

        uint64_t multiple = static_cast<uint64_t>(divisor) * divisor;
        const uint64_t first_multiple =
            ((static_cast<uint64_t>(First) + divisor - 1u) / divisor) * divisor;
        if (multiple < first_multiple) {
            multiple = first_multiple;
        }
        for (; multiple < LastExclusive; multiple += divisor) {
            data.composite[static_cast<size_t>(multiple - First)] = true;
        }
    }

    size_t count = 0;
    for (bool is_composite : data.composite) {
        if (!is_composite) {
            ++count;
        }
    }
    data.prime_count = count;

    return data;
}

namespace detail {
template <uint32_t First, uint32_t LastExclusive>
inline constexpr auto prime_segment_data = make_prime_segment_data<First, LastExclusive>();
} // namespace detail

template <std::unsigned_integral T, uint32_t First, uint32_t LastExclusive>
consteval auto generate_primes_in_range() {
    static_assert(LastExclusive <= static_cast<uint32_t>(std::numeric_limits<T>::max()),
                  "prime range does not fit in the requested output type");

    constexpr auto segment = detail::prime_segment_data<First, LastExclusive>;
    std::array<T, segment.prime_count> primes{};
    size_t out = 0;
    for (uint32_t value = First; value < LastExclusive; ++value) {
        if (!segment.composite[value - First]) {
            primes[out++] = static_cast<T>(value);
        }
    }
    return primes;
}

inline constexpr auto cri_key_primes =
    generate_primes_in_range<uint16_t, 0x401B, 0x683A>();
static_assert(cri_key_primes.size() == 0x400);

} // namespace cricodecs::util
