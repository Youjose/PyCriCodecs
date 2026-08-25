#pragma once
/**
 * @file io_reader.hpp
 * @brief Shared byte-span and file-backed reader utilities.
 *
 * Project-local IO abstraction for CriCodecs parsers, with platform-backed
 * file loading where available.
 */

#include <filesystem>
#include <memory>
#include <span>
#include <expected>
#include <cstdint>
#include <cstring>
#include <concepts>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include "io_endian.hpp"

namespace cricodecs::io {

enum class access_pattern : uint8_t {
    normal,
    sequential,
    random
};

struct SourceView {
    using Owner = std::shared_ptr<const void>;

    std::span<const uint8_t> bytes{};
    Owner owner{};

    SourceView() noexcept = default;

    SourceView(std::span<const uint8_t> bytes, Owner owner = {})
        : bytes(bytes), owner(std::move(owner)) {}

    [[nodiscard]] const uint8_t* data() const noexcept { return bytes.data(); }
    [[nodiscard]] size_t size() const noexcept { return bytes.size(); }
    [[nodiscard]] bool empty() const noexcept { return bytes.empty(); }
    [[nodiscard]] std::span<const uint8_t> subspan(size_t offset, size_t count = std::dynamic_extent) const noexcept {
        return bytes.subspan(offset, count);
    }
    [[nodiscard]] const uint8_t* begin() const noexcept { return bytes.data(); }
    [[nodiscard]] const uint8_t* end() const noexcept { return bytes.data() + bytes.size(); }
    [[nodiscard]] operator std::span<const uint8_t>() const noexcept { return bytes; }
};

namespace detail {
#if defined(_WIN32)
    struct win32_reader_handles;
#else
    struct posix_reader_descriptor;
#endif
    struct fallback_reader_state;
} // namespace detail

class bit_reader {
public:
    constexpr bit_reader() noexcept = default;

    constexpr bit_reader(const uint8_t* data, size_t byte_size) noexcept
        : m_data(data), m_size(byte_size * 8), m_pos(0) {}

    constexpr explicit bit_reader(std::span<const uint8_t> data) noexcept
        : m_data(data.data()), m_size(data.size() * 8), m_pos(0) {}

    // Apply a byte substitution table lazily inside the requested byte range.
    // This keeps encrypted bit grammars on the same reader as clear data.
    constexpr bit_reader(
        std::span<const uint8_t> data,
        std::span<const uint8_t, 256> byte_map,
        size_t mapped_begin,
        size_t mapped_end) noexcept
        : m_data(data.data())
        , m_size(data.size() * 8)
        , m_map(byte_map.data())
        , m_mapped_begin(std::min(mapped_begin, data.size()))
        , m_mapped_end(std::min(mapped_end, data.size())) {}

    [[nodiscard]] constexpr uint32_t read(int bits) noexcept {
        if (bits <= 0 || bits > 32 || remaining() < static_cast<size_t>(bits)) {
            m_valid = false;
            return 0;
        }
        uint32_t result = 0;
        // Optimized: process byte-aligned chunks where possible
        int remaining = bits;
        while (remaining > 0) {
            size_t byte_pos = m_pos >> 3;
            int bit_offset = static_cast<int>(m_pos & 7);
            int available = 8 - bit_offset;
            int take = remaining < available ? remaining : available;
            uint32_t mask = (1u << take) - 1u;
            uint32_t val = (byte(byte_pos) >> (available - take)) & mask;
            result = (result << take) | val;
            m_pos += take;
            remaining -= take;
        }
        return result;
    }

    [[nodiscard]] constexpr std::expected<uint32_t, const char*> read_checked(int bits) noexcept {
        if (bits <= 0) {
            return std::unexpected("I/O bit reader failed: bit count must be positive");
        }
        if (bits > 32) {
            return std::unexpected("I/O bit reader failed: bit count exceeds 32");
        }
        if (remaining() < static_cast<size_t>(bits)) {
            return std::unexpected("I/O bit reader failed: read is out of bounds");
        }
        return read(bits);
    }

    [[nodiscard]] constexpr uint32_t peek(int bits) noexcept {
        const size_t saved_position = m_pos;
        const bool saved_valid = m_valid;
        const uint32_t result = read(bits);
        m_pos = saved_position;
        m_valid = saved_valid;
        return result;
    }

    constexpr void skip(int bits) noexcept {
        if (bits >= 0) {
            m_pos += std::min(static_cast<size_t>(bits), remaining());
            return;
        }
        const auto delta = static_cast<size_t>(-static_cast<int64_t>(bits));
        m_pos -= std::min(delta, m_pos);
    }
    [[nodiscard]] constexpr size_t position() const noexcept { return m_pos; }
    [[nodiscard]] constexpr size_t remaining() const noexcept { return m_size > m_pos ? m_size - m_pos : 0; }
    [[nodiscard]] constexpr bool valid() const noexcept { return m_valid; }
    [[nodiscard]] constexpr uint8_t byte(size_t position) const noexcept {
        const uint8_t value = m_data[position];
        return m_map != nullptr && position >= m_mapped_begin && position < m_mapped_end
            ? m_map[value]
            : value;
    }
    constexpr void set_position(size_t pos) noexcept { m_pos = pos; }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;   // in bits
    size_t m_pos = 0;    // in bits
    const uint8_t* m_map = nullptr;
    size_t m_mapped_begin = 0;
    size_t m_mapped_end = 0;
    bool m_valid = true;
};

class bit_writer {
public:
    constexpr bit_writer() noexcept = default;

    constexpr bit_writer(uint8_t* data, size_t byte_size) noexcept
        : m_data(data), m_size(byte_size * 8), m_pos(0) {}

    constexpr explicit bit_writer(std::span<uint8_t> data) noexcept
        : m_data(data.data()), m_size(data.size() * 8), m_pos(0) {}

    constexpr void set_buffer(uint8_t* data, size_t byte_size) noexcept {
        m_data = data;
        m_size = byte_size * 8;
        m_pos = 0;
    }

    constexpr void set_buffer(std::span<uint8_t> data) noexcept {
        set_buffer(data.data(), data.size());
    }

    constexpr void write(uint32_t value, int bits) noexcept {
        if (bits <= 0 || bits > 32 || m_pos > m_size
            || static_cast<size_t>(bits) > m_size - m_pos) return;
        // Optimized: process byte-aligned chunks
        int remaining = bits;
        while (remaining > 0) {
            size_t byte_pos = m_pos >> 3;
            int bit_offset = static_cast<int>(m_pos & 7);
            int available = 8 - bit_offset;
            int take = remaining < available ? remaining : available;
            uint8_t mask = static_cast<uint8_t>(((1u << take) - 1u) << (available - take));
            uint8_t val = static_cast<uint8_t>(((value >> (remaining - take)) & ((1u << take) - 1u)) << (available - take));
            m_data[byte_pos] = (m_data[byte_pos] & ~mask) | val;
            m_pos += take;
            remaining -= take;
        }
    }

    constexpr void align(int bits) noexcept {
        if (bits <= 0) return;
        int rem = static_cast<int>(m_pos % bits);
        if (rem != 0) m_pos += bits - rem;
    }

    [[nodiscard]] constexpr size_t position() const noexcept { return m_pos; }
    constexpr void set_position(size_t pos) noexcept { m_pos = pos; }

private:
    uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_pos = 0;
};

class reader {
public:
    reader() noexcept;
    ~reader() noexcept;
    reader(const reader&) = delete;
    reader& operator=(const reader&) = delete;
    reader(reader&&) noexcept;
    reader& operator=(reader&&) noexcept;

    std::expected<void, const char*> open(const std::filesystem::path& path) noexcept;
    std::expected<void, const char*> open(const std::filesystem::path& path, access_pattern pattern) noexcept;
    std::expected<void, const char*> open(const uint8_t* data, size_t size) noexcept;
    std::expected<void, const char*> open(std::span<const uint8_t> data) noexcept {
        return open(data.data(), data.size());
    }
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

    [[nodiscard]] std::span<const uint8_t> data() const noexcept;
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] size_t tell() const noexcept { return m_cursor; }
    [[nodiscard]] size_t remaining() const noexcept { return m_data_size > m_cursor ? m_data_size - m_cursor : 0; }
    
    void seek(size_t pos) noexcept {
        m_cursor = pos > m_data_size ? m_data_size : pos;
    }
    
    void skip(size_t n) noexcept {
        const size_t available = remaining();
        m_cursor += n > available ? available : n;
    }

    // These are the fast-path: no std::expected overhead, return T{} on OOB
    template<EndianSwappable T>
    T read_le() noexcept {
        if (remaining() < sizeof(T)) return T{};
        T val = io::read_le<T>(m_data_ptr + m_cursor);
        m_cursor += sizeof(T);
        return val;
    }

    template<EndianSwappable T>
    T read_be() noexcept {
        if (remaining() < sizeof(T)) return T{};
        T val = io::read_be<T>(m_data_ptr + m_cursor);
        m_cursor += sizeof(T);
        return val;
    }

    template<EndianSwappable T>
    T read_le_at(size_t offset) const noexcept {
        if (offset > m_data_size || m_data_size - offset < sizeof(T)) return T{};
        return io::read_le<T>(m_data_ptr + offset);
    }

    template<EndianSwappable T>
    T read_be_at(size_t offset) const noexcept {
        if (offset > m_data_size || m_data_size - offset < sizeof(T)) return T{};
        return io::read_be<T>(m_data_ptr + offset);
    }

    template<Readable T>
    std::expected<T, const char*> read_struct(size_t offset) const noexcept {
        if (offset > m_data_size || m_data_size - offset < sizeof(T)) {
            return std::unexpected("I/O reader failed: read operation is out of bounds");
        }
        T result{};
        std::memcpy(&result, m_data_ptr + offset, sizeof(T));
        return result;
    }

    [[nodiscard]] size_t read_at(size_t offset, std::span<uint8_t> output) const noexcept {
        if (output.empty() || offset >= m_data_size || m_data_ptr == nullptr) return 0;
        const size_t count = std::min(output.size(), m_data_size - offset);
        std::memcpy(output.data(), m_data_ptr + offset, count);
        return count;
    }

    std::span<const uint8_t> read_bytes(size_t count) noexcept {
        const size_t available = remaining();
        if (count > available) count = available;
        const uint8_t* ptr = count == 0 ? nullptr : m_data_ptr + m_cursor;
        auto sub = std::span<const uint8_t>(ptr, count);
        m_cursor += count;
        return sub;
    }

    std::span<const uint8_t> subspan(size_t offset, size_t count) const noexcept {
        if (offset >= m_data_size) return {};
        const size_t available = m_data_size - offset;
        if (count > available) count = available;
        return std::span<const uint8_t>(m_data_ptr + offset, count);
    }

private:
    // Platform-specific file mapping state
#if defined(_WIN32)
    std::unique_ptr<detail::win32_reader_handles> m_handles;
    std::expected<void, const char*> open_file_impl(const std::filesystem::path& path, access_pattern pattern) noexcept;
#elif !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
    std::unique_ptr<detail::posix_reader_descriptor> m_descriptor;
    std::expected<void, const char*> open_file_impl(const std::filesystem::path& path, access_pattern pattern) noexcept;
#else
    std::unique_ptr<detail::fallback_reader_state> m_fallback;
    std::expected<void, const char*> open_file_impl(const std::filesystem::path& path, access_pattern pattern) noexcept;
#endif

    const uint8_t* m_data_ptr = nullptr;
    size_t m_data_size = 0;
    size_t m_cursor = 0;
    bool m_has_external_source = false;  // true when bound to caller-owned memory, including empty spans
};

[[nodiscard]] inline std::expected<std::vector<uint8_t>, std::string> read_file_bytes(
    const std::filesystem::path& path,
    std::string_view context = "Failed to read file")
{
    reader file_reader;
    if (auto result = file_reader.open(path); !result) {
        return std::unexpected(
            std::string(context) + ": failed to open " + path.string() + " (" + result.error() + ")");
    }

    const auto data = file_reader.data();
    return std::vector<uint8_t>(data.begin(), data.end());
}

} // namespace cricodecs::io
