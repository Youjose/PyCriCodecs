#pragma once
/**
 * @file io_writer.hpp
 * @brief Shared byte/file writer utilities.
 *
 * Project-local output abstraction for CriCodecs builders and extractors.
 * Implemented by Youjose.
 */

#include <cstdint>
#include <cstddef>
#include <array>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "io_endian.hpp"

namespace cricodecs::io {

namespace detail {
#if !defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__)
    struct fallback_writer_state;
#endif
} // namespace detail

class writer {
public:
    static constexpr size_t DEFAULT_BUFFER_SIZE = 64 * 1024; // 64KB

    writer() = default;
    ~writer();
    writer(const writer&) = delete;
    writer& operator=(const writer&) = delete;
    writer(writer&&) noexcept;
    writer& operator=(writer&&) noexcept;

    std::expected<void, const char*> open(const std::filesystem::path& path,
                                          size_t buffer_size = DEFAULT_BUFFER_SIZE);
    std::expected<void, const char*> close() noexcept;

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] size_t bytes_written() const noexcept { return m_total_written + m_buffer_pos; }

    std::expected<void, const char*> write(const void* data, size_t size);
    std::expected<void, const char*> write(std::span<const uint8_t> data);
    std::expected<void, const char*> flush();

    template<EndianSwappable T>
    void write_le(T value) noexcept {
        std::array<uint8_t, sizeof(T)> bytes;
        io::write_le<T>(bytes.data(), value);
        write_to_buffer(bytes.data(), bytes.size());
    }

    template<EndianSwappable T>
    void write_be(T value) noexcept {
        std::array<uint8_t, sizeof(T)> bytes;
        io::write_be<T>(bytes.data(), value);
        write_to_buffer(bytes.data(), bytes.size());
    }

    void write_bytes(std::span<const uint8_t> data) noexcept {
        write_to_buffer(data.data(), data.size());
    }

    void write_string(std::string_view str) noexcept {
        write_to_buffer(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }

    void write_zeros(size_t count) noexcept;

private:
#if !defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__)
    std::unique_ptr<detail::fallback_writer_state> m_fallback;
#else
    void* m_handle = nullptr;           // Platform-specific handle
#endif
    std::vector<uint8_t> m_buffer;
    size_t m_buffer_pos = 0;
    size_t m_total_written = 0;
    bool m_buffer_zeroed = false;
    bool m_write_failed = false;

    std::expected<void, const char*> open_file(const std::filesystem::path& path);
    std::expected<void, const char*> close_file() noexcept;
    bool write_direct(const uint8_t* data, size_t size) noexcept;
    void write_to_buffer(const uint8_t* data, size_t size) noexcept;
    std::expected<void, const char*> flush_buffer();
};

[[nodiscard]] inline std::expected<void, std::string> write_file_bytes(
    const std::filesystem::path& path,
    std::span<const uint8_t> bytes,
    std::string_view context = "Failed to write file")
{
    writer file_writer;
    if (auto result = file_writer.open(path); !result) {
        return std::unexpected(
            std::string(context) + ": failed to open " + path.string() + " (" + result.error() + ")");
    }
    if (auto result = file_writer.write(bytes); !result) {
        return std::unexpected(
            std::string(context) + ": failed to write " + path.string() + " (" + result.error() + ")");
    }
    if (auto result = file_writer.close(); !result) {
        return std::unexpected(
            std::string(context) + ": failed to close " + path.string() + " (" + result.error() + ")");
    }
    return {};
}

} // namespace cricodecs::io
