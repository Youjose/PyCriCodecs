#pragma once

#include <cstdint>
#include <simd>
#include <span>

namespace cricodecs::simd {

using bytes32 = std::simd::vec<std::uint8_t, 32>;
using bytes64 = std::simd::vec<std::uint8_t, 64>;
using words16 = std::simd::vec<std::uint16_t, 16>;

template <class V>
[[nodiscard]] inline V load(const typename V::value_type* data) noexcept {
    return std::simd::unchecked_load<V>(
        std::span<const typename V::value_type, V::size()>(data, V::size()));
}

template <class V>
inline void store(const V& value, typename V::value_type* data) noexcept {
    std::simd::unchecked_store(
        value,
        std::span<typename V::value_type, V::size()>(data, V::size()));
}

template <class V, class T>
[[nodiscard]] inline V load_as(const T* data) noexcept {
    return std::simd::unchecked_load<V>(
        std::span<const T, V::size()>(data, V::size()), std::simd::flag_convert);
}

template <class V, class T>
inline void store_as(const V& value, T* data) noexcept {
    std::simd::unchecked_store(
        value, std::span<T, V::size()>(data, V::size()), std::simd::flag_convert);
}

} // namespace cricodecs::simd
