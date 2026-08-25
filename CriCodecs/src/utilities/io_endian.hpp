#pragma once
/**
 * @file io_endian.hpp
 * @brief Endian-safe integer and byte helpers.
 *
 * Project-local serialization helpers used across the codec/container modules.
 */

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace cricodecs::io {

struct FourCC {
    std::array<uint8_t, 4> bytes{};

    consteval explicit FourCC(const char (&text)[5]) noexcept
        : bytes{
            static_cast<uint8_t>(text[0]),
            static_cast<uint8_t>(text[1]),
            static_cast<uint8_t>(text[2]),
            static_cast<uint8_t>(text[3]),
        } {}

    constexpr FourCC(uint8_t first, uint8_t second, uint8_t third, uint8_t fourth) noexcept
        : bytes{first, second, third, fourth} {}

    [[nodiscard]] constexpr uint32_t be_value() const noexcept {
        return (static_cast<uint32_t>(bytes[0]) << 24u) |
            (static_cast<uint32_t>(bytes[1]) << 16u) |
            (static_cast<uint32_t>(bytes[2]) << 8u) |
            static_cast<uint32_t>(bytes[3]);
    }

    [[nodiscard]] constexpr uint32_t le_value() const noexcept {
        return static_cast<uint32_t>(bytes[0]) |
            (static_cast<uint32_t>(bytes[1]) << 8u) |
            (static_cast<uint32_t>(bytes[2]) << 16u) |
            (static_cast<uint32_t>(bytes[3]) << 24u);
    }

    [[nodiscard]] constexpr auto begin() const noexcept { return bytes.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept { return bytes.end(); }
    [[nodiscard]] constexpr size_t size() const noexcept { return bytes.size(); }
};

struct Int24 {
    uint8_t val[3];

    constexpr operator int32_t() const noexcept {
        int32_t result = (val[2] << 16) | (val[1] << 8) | val[0];
        if (result & 0x800000) {
            result |= 0xFF000000;
        }
        return result;
    }
};

template<typename T>
concept Readable = std::is_trivially_copyable_v<T>;

template<typename T>
concept EndianSwappable = (std::is_arithmetic_v<T> || std::is_same_v<T, Int24>) 
                          && std::is_trivially_copyable_v<T>;

template<EndianSwappable T>
inline T read_le(const uint8_t* buf) noexcept {
    T val;
    std::memcpy(&val, buf, sizeof(T));
    if constexpr (sizeof(T) > 1 && std::endian::native == std::endian::big) {
        if constexpr (std::is_same_v<T, Int24>) {
            std::swap(val.val[0], val.val[2]);
        } else {
            val = std::byteswap(val);
        }
    }
    return val;
}

template<EndianSwappable T>
inline T read_be(const uint8_t* buf) noexcept {
    T val;
    std::memcpy(&val, buf, sizeof(T));
    if constexpr (sizeof(T) > 1 && std::endian::native == std::endian::little) {
        if constexpr (std::is_same_v<T, Int24>) {
            std::swap(val.val[0], val.val[2]);
        } else {
            val = std::byteswap(val);
        }
    }
    return val;
}

template<EndianSwappable T>
[[nodiscard]] inline T read_be(std::span<const uint8_t> buf, size_t offset) noexcept {
    if (offset > buf.size() || sizeof(T) > buf.size() - offset) {
        return T{};
    }
    return read_be<T>(buf.data() + offset);
}

template<EndianSwappable T>
inline void write_le(uint8_t* buf, T val) noexcept {
    if constexpr (sizeof(T) > 1 && std::endian::native == std::endian::big) {
        if constexpr (std::is_same_v<T, Int24>) {
            std::swap(val.val[0], val.val[2]);
        } else {
            val = std::byteswap(val);
        }
    }
    std::memcpy(buf, &val, sizeof(T));
}

template<EndianSwappable T>
inline void write_be(uint8_t* buf, T val) noexcept {
    if constexpr (sizeof(T) > 1 && std::endian::native == std::endian::little) {
        if constexpr (std::is_same_v<T, Int24>) {
            std::swap(val.val[0], val.val[2]);
        } else {
            val = std::byteswap(val);
        }
    }
    std::memcpy(buf, &val, sizeof(T));
}

template<std::unsigned_integral T>
[[nodiscard]] inline T read_le_n(const uint8_t* buf, uint8_t n) noexcept {
    T value = 0;
    for (uint8_t i = 0; i < n && i < sizeof(T); ++i) {
        value |= static_cast<T>(buf[i]) << (i * 8u);
    }
    return value;
}

template<std::unsigned_integral T>
inline void write_le_n(uint8_t* buf, T value, uint8_t n) noexcept {
    for (uint8_t i = 0; i < n && i < sizeof(T); ++i) {
        buf[i] = static_cast<uint8_t>(value >> (i * 8u));
    }
}

template<EndianSwappable T>
inline void append_le(std::vector<uint8_t>& buffer, T val) {
    const auto position = buffer.size();
    buffer.resize(position + sizeof(T));
    write_le<T>(buffer.data() + position, val);
}

template<EndianSwappable T>
inline void append_be(std::vector<uint8_t>& buffer, T val) {
    const auto position = buffer.size();
    buffer.resize(position + sizeof(T));
    write_be<T>(buffer.data() + position, val);
}

static_assert(FourCC{"RIFF"}.be_value() == 0x52494646u);
static_assert(FourCC{"RIFF"}.le_value() == 0x46464952u);

} // namespace cricodecs::io
