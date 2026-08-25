/**
 * @file io_writer_platform.cpp
 * @brief Buffered output with small platform-specific file operations.
 */

#include "io_writer.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#else
#include <fstream>
#endif

namespace cricodecs::io {

#if !defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__)
namespace detail {
struct fallback_writer_state {
    explicit fallback_writer_state(const std::filesystem::path& path)
        : file(path, std::ios::binary | std::ios::out) {}

    std::ofstream file;
};
} // namespace detail
#endif

writer::~writer() { static_cast<void>(close()); }

writer::writer(writer&& other) noexcept
#if !defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__)
    : m_fallback(std::move(other.m_fallback)),
#else
    : m_handle(std::exchange(other.m_handle, nullptr)),
#endif
      m_buffer(std::move(other.m_buffer)),
      m_buffer_pos(std::exchange(other.m_buffer_pos, 0)),
      m_total_written(std::exchange(other.m_total_written, 0)),
      m_buffer_zeroed(std::exchange(other.m_buffer_zeroed, false)),
      m_write_failed(std::exchange(other.m_write_failed, false)) {}

writer& writer::operator=(writer&& other) noexcept {
    if (this == &other) return *this;

    static_cast<void>(close());
#if !defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__)
    m_fallback = std::move(other.m_fallback);
#else
    m_handle = std::exchange(other.m_handle, nullptr);
#endif
    m_buffer = std::move(other.m_buffer);
    m_buffer_pos = std::exchange(other.m_buffer_pos, 0);
    m_total_written = std::exchange(other.m_total_written, 0);
    m_buffer_zeroed = std::exchange(other.m_buffer_zeroed, false);
    m_write_failed = std::exchange(other.m_write_failed, false);
    return *this;
}

std::expected<void, const char*> writer::open(
    const std::filesystem::path& path,
    size_t buffer_size)
{
    if (auto result = close(); !result) return result;
    if (auto result = open_file(path); !result) return result;

    m_buffer.resize(buffer_size);
    m_buffer_pos = 0;
    m_total_written = 0;
    m_buffer_zeroed = true;
    m_write_failed = false;
    return {};
}

std::expected<void, const char*> writer::close() noexcept {
    if (!is_open()) return {};

    const auto reset = [this] {
        m_buffer.clear();
        m_buffer_pos = 0;
        m_buffer_zeroed = false;
        m_write_failed = false;
    };

    if (m_write_failed) {
        static_cast<void>(close_file());
        reset();
        return std::unexpected("I/O writer failed: write failed");
    }
    if (m_buffer_pos != 0) {
        if (auto result = flush_buffer(); !result) {
            static_cast<void>(close_file());
            reset();
            return result;
        }
    }

    auto result = close_file();
    reset();
    return result;
}

bool writer::is_open() const noexcept {
#if !defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__)
    return m_fallback && m_fallback->file.is_open();
#else
    return m_handle != nullptr;
#endif
}

std::expected<void, const char*> writer::write(const void* data, size_t size) {
    if (!is_open()) return std::unexpected("I/O writer failed: file is not open");
    write_to_buffer(static_cast<const uint8_t*>(data), size);
    if (m_write_failed) return std::unexpected("I/O writer failed: write failed");
    return {};
}

std::expected<void, const char*> writer::write(std::span<const uint8_t> data) {
    return write(data.data(), data.size());
}

std::expected<void, const char*> writer::flush() {
    if (!is_open()) return std::unexpected("I/O writer failed: file is not open");
    if (m_write_failed) return std::unexpected("I/O writer failed: write failed");
    return flush_buffer();
}

std::expected<void, const char*> writer::open_file(const std::filesystem::path& path) {
#if defined(_WIN32)
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::unexpected("I/O writer failed: could not create file");
    }
    m_handle = file;
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file < 0) return std::unexpected("I/O writer failed: could not create file");
    m_handle = reinterpret_cast<void*>(static_cast<intptr_t>(file) + 1);
#else
    m_fallback = std::make_unique<detail::fallback_writer_state>(path);
    if (!m_fallback->file.is_open()) {
        m_fallback.reset();
        return std::unexpected("I/O writer failed: could not create file");
    }
#endif
    return {};
}

std::expected<void, const char*> writer::close_file() noexcept {
#if defined(_WIN32)
    const bool closed = CloseHandle(static_cast<HANDLE>(m_handle));
    m_handle = nullptr;
    if (!closed) return std::unexpected("I/O writer failed: could not close file");
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
    const int file = static_cast<int>(reinterpret_cast<intptr_t>(m_handle) - 1);
    m_handle = nullptr;
    if (::close(file) != 0) return std::unexpected("I/O writer failed: could not close file");
#else
    m_fallback->file.close();
    const bool failed = m_fallback->file.fail();
    m_fallback.reset();
    if (failed) return std::unexpected("I/O writer failed: could not close file");
#endif
    return {};
}

bool writer::write_direct(const uint8_t* data, size_t size) noexcept {
#if defined(_WIN32)
    size_t written_total = 0;
    while (written_total < size) {
        const size_t remaining = size - written_total;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(
            remaining, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(
                static_cast<HANDLE>(m_handle), data + written_total, chunk, &written, nullptr)
            || written == 0) {
            return false;
        }
        written_total += written;
    }
    return true;
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
    const int file = static_cast<int>(reinterpret_cast<intptr_t>(m_handle) - 1);
    size_t written_total = 0;
    while (written_total < size) {
        const size_t chunk = std::min(
            size - written_total,
            static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
        ssize_t written;
        do {
            written = ::write(file, data + written_total, chunk);
        } while (written < 0 && errno == EINTR);
        if (written <= 0) return false;
        written_total += static_cast<size_t>(written);
    }
    return true;
#else
    size_t written = 0;
    while (written < size) {
        const size_t chunk = std::min(
            size - written,
            static_cast<size_t>(std::numeric_limits<std::streamsize>::max()));
        m_fallback->file.write(
            reinterpret_cast<const char*>(data + written),
            static_cast<std::streamsize>(chunk));
        if (m_fallback->file.fail()) return false;
        written += chunk;
    }
    return true;
#endif
}

void writer::write_to_buffer(const uint8_t* data, size_t size) noexcept {
    if (size == 0 || !is_open() || m_write_failed) return;
    if (m_buffer.empty()) {
        m_write_failed = !write_direct(data, size);
        if (!m_write_failed) m_total_written += size;
        return;
    }
    if (size >= m_buffer.size()) {
        if (m_buffer_pos != 0 && !flush_buffer()) return;
        m_write_failed = !write_direct(data, size);
        if (!m_write_failed) m_total_written += size;
        return;
    }

    while (size != 0) {
        const size_t count = std::min(size, m_buffer.size() - m_buffer_pos);
        std::memcpy(m_buffer.data() + m_buffer_pos, data, count);
        m_buffer_pos += count;
        m_buffer_zeroed = false;
        data += count;
        size -= count;
        if (m_buffer_pos == m_buffer.size() && !flush_buffer()) return;
    }
}

std::expected<void, const char*> writer::flush_buffer() {
    if (m_write_failed) return std::unexpected("I/O writer failed: write failed");
    if (m_buffer_pos == 0) return {};
    if (!write_direct(m_buffer.data(), m_buffer_pos)) {
        m_write_failed = true;
        return std::unexpected("I/O writer failed: write failed");
    }
    m_total_written += m_buffer_pos;
    m_buffer_pos = 0;
    return {};
}

void writer::write_zeros(size_t count) noexcept {
    if (count == 0 || !is_open() || m_write_failed) return;
    if (m_buffer.empty()) {
        std::array<uint8_t, 4096> zeros{};
        while (count != 0) {
            const size_t chunk = std::min(count, zeros.size());
            write_to_buffer(zeros.data(), chunk);
            count -= chunk;
        }
        return;
    }

    if (m_buffer_pos != 0) {
        const size_t chunk = std::min(count, m_buffer.size() - m_buffer_pos);
        std::memset(m_buffer.data() + m_buffer_pos, 0, chunk);
        m_buffer_pos += chunk;
        count -= chunk;
        if (m_buffer_pos == m_buffer.size() && !flush_buffer()) return;
    }
    if (count == 0) return;

    if (!m_buffer_zeroed) {
        std::memset(m_buffer.data(), 0, m_buffer.size());
        m_buffer_zeroed = true;
    }
    while (count >= m_buffer.size()) {
        if (!write_direct(m_buffer.data(), m_buffer.size())) {
            m_write_failed = true;
            return;
        }
        m_total_written += m_buffer.size();
        count -= m_buffer.size();
    }
    if (count != 0) {
        m_buffer_pos = count;
    }
}

} // namespace cricodecs::io
