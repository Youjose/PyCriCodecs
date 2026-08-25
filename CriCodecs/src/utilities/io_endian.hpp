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
#include <meta>
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
concept Scalar = ((std::is_arithmetic_v<T> || std::is_enum_v<T>) &&
                  !std::same_as<T, bool> &&
                  (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8)) ||
                 std::same_as<T, Int24>;

template <typename T>
using Bits = std::conditional_t<sizeof(T) == 1, uint8_t,
             std::conditional_t<sizeof(T) == 2, uint16_t,
             std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>>>;

template <typename T>
inline constexpr auto data_members = std::define_static_array(
    std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unprivileged()));

template <typename T>
struct StdArray;

template <typename T, size_t Size>
struct StdArray<std::array<T, Size>> {
    using value_type = T;
    static constexpr size_t size = Size;
};

template <typename T>
concept StandardArray = requires { typename StdArray<std::remove_cv_t<T>>::value_type; };

template <typename T>
consteval size_t serialized_size() {
    if constexpr (Scalar<T>) {
        return sizeof(T);
    } else if constexpr (std::is_bounded_array_v<T>) {
        return std::extent_v<T> * serialized_size<std::remove_extent_t<T>>();
    } else if constexpr (StandardArray<T>) {
        using Array = StdArray<std::remove_cv_t<T>>;
        return Array::size * serialized_size<typename Array::value_type>();
    } else if constexpr (std::is_aggregate_v<T> && std::is_trivially_copyable_v<T> &&
                         std::is_standard_layout_v<T>) {
        size_t size = 0;
        bool valid = !data_members<T>.empty();
        template for (constexpr auto member : data_members<T>) {
            using M = [:std::meta::type_of(member):];
            constexpr size_t member_size = serialized_size<M>();
            valid = valid && member_size != 0 && !std::meta::is_bit_field(member);
            size += member_size;
        }
        return valid ? size : 0;
    } else {
        return 0;
    }
}

template <typename T>
concept EndianSwappable = serialized_size<T>() == sizeof(T);

template <std::endian Order, typename T>
void read_value(const uint8_t*& source, T& result) noexcept {
    if constexpr (std::same_as<T, Int24>) {
        std::memcpy(&result, source, sizeof(T));
        std::swap(result.val[0], result.val[2]);
        source += sizeof(T);
    } else if constexpr (Scalar<T>) {
        Bits<T> bits;
        std::memcpy(&bits, source, sizeof(T));
        if constexpr (sizeof(T) > 1) bits = std::byteswap(bits);
        result = std::bit_cast<T>(bits);
        source += sizeof(T);
    } else if constexpr (std::is_bounded_array_v<T>) {
        for (auto& element : result) read_value<Order>(source, element);
    } else if constexpr (StandardArray<T>) {
        for (auto& element : result) read_value<Order>(source, element);
    } else {
        template for (constexpr auto member : data_members<T>) {
            read_value<Order>(source, result.[:member:]);
        }
    }
}

template <std::endian Order, typename T>
void write_value(uint8_t*& destination, const T& value) noexcept {
    if constexpr (std::same_as<T, Int24>) {
        auto encoded = value;
        std::swap(encoded.val[0], encoded.val[2]);
        std::memcpy(destination, &encoded, sizeof(T));
        destination += sizeof(T);
    } else if constexpr (Scalar<T>) {
        auto bits = std::bit_cast<Bits<T>>(value);
        if constexpr (sizeof(T) > 1) bits = std::byteswap(bits);
        std::memcpy(destination, &bits, sizeof(T));
        destination += sizeof(T);
    } else if constexpr (std::is_bounded_array_v<T>) {
        for (const auto& element : value) write_value<Order>(destination, element);
    } else if constexpr (StandardArray<T>) {
        for (const auto& element : value) write_value<Order>(destination, element);
    } else {
        template for (constexpr auto member : data_members<T>) {
            write_value<Order>(destination, value.[:member:]);
        }
    }
}

template <std::endian Order, EndianSwappable T>
[[nodiscard]] T read_struct(const uint8_t* source) noexcept {
    T result{};
    if constexpr (Order == std::endian::native) {
        std::memcpy(&result, source, sizeof(T));
    } else {
        read_value<Order>(source, result);
    }
    return result;
}

template <std::endian Order, EndianSwappable T>
void read_structs(const uint8_t* source, std::span<T> results) noexcept {
    if (results.empty()) return;
    if constexpr (Order == std::endian::native) {
        std::memcpy(results.data(), source, results.size_bytes());
    } else {
        for (auto& result : results) {
            read_value<Order>(source, result);
        }
    }
}

template <std::endian Order, EndianSwappable T>
uint8_t* write_struct(uint8_t* destination, const T& value) noexcept {
    if constexpr (Order == std::endian::native) {
        std::memcpy(destination, &value, sizeof(T));
        destination += sizeof(T);
    } else {
        write_value<Order>(destination, value);
    }
    return destination;
}

template <std::endian Order, EndianSwappable T>
uint8_t* write_structs(uint8_t* destination, std::span<const T> values) noexcept {
    if (values.empty()) return destination;
    if constexpr (Order == std::endian::native) {
        std::memcpy(destination, values.data(), values.size_bytes());
        destination += values.size_bytes();
    } else {
        for (const auto& value : values) {
            write_value<Order>(destination, value);
        }
    }
    return destination;
}

template <EndianSwappable T>
[[nodiscard]] inline T read_le(const uint8_t* source) noexcept {
    return read_struct<std::endian::little, T>(source);
}

template <EndianSwappable T>
[[nodiscard]] inline T read_be(const uint8_t* source) noexcept {
    return read_struct<std::endian::big, T>(source);
}

template <EndianSwappable T>
[[nodiscard]] inline T read_be(std::span<const uint8_t> source, size_t offset) noexcept {
    return offset <= source.size() && sizeof(T) <= source.size() - offset
        ? read_be<T>(source.data() + offset) : T{};
}

template <EndianSwappable T>
inline uint8_t* write_le(uint8_t* destination, const T& value) noexcept {
    return write_struct<std::endian::little>(destination, value);
}

template <EndianSwappable T>
inline uint8_t* write_le(uint8_t* destination, std::span<const T> values) noexcept {
    return write_structs<std::endian::little>(destination, values);
}

template <EndianSwappable T>
inline uint8_t* write_be(uint8_t* destination, const T& value) noexcept {
    return write_struct<std::endian::big>(destination, value);
}

template <EndianSwappable T>
inline uint8_t* write_be(uint8_t* destination, std::span<const T> values) noexcept {
    return write_structs<std::endian::big>(destination, values);
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
