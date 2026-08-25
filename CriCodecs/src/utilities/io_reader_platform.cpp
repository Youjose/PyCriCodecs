/**
 * @file io_reader_platform.cpp
 * @brief Memory and platform file sources for io::reader.
 */

#include "io_reader.hpp"

#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <cstdio>
#include <cstdlib>
#endif

namespace cricodecs::io {

namespace detail {

#if defined(_WIN32)
struct win32_reader_handles {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
};
#elif !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
struct posix_reader_descriptor {
    int fd = -1;
};
#else
struct fallback_reader_state {
    uint8_t* data = nullptr;

    ~fallback_reader_state() { std::free(data); }
};
#endif

} // namespace detail

reader::reader() noexcept = default;

reader::~reader() noexcept { close(); }

reader::reader(reader&& other) noexcept
#if defined(_WIN32)
    : m_handles(std::move(other.m_handles)),
#elif !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
    : m_descriptor(std::move(other.m_descriptor)),
#else
    : m_fallback(std::move(other.m_fallback)),
#endif
      m_data_ptr(std::exchange(other.m_data_ptr, nullptr)),
      m_data_size(std::exchange(other.m_data_size, 0)),
      m_cursor(std::exchange(other.m_cursor, 0)),
      m_pattern(std::exchange(other.m_pattern, access_pattern::normal)),
      m_has_external_source(std::exchange(other.m_has_external_source, false)) {}

reader& reader::operator=(reader&& other) noexcept {
    if (this == &other) return *this;

    close();
#if defined(_WIN32)
    m_handles = std::move(other.m_handles);
#elif !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
    m_descriptor = std::move(other.m_descriptor);
#else
    m_fallback = std::move(other.m_fallback);
#endif
    m_data_ptr = std::exchange(other.m_data_ptr, nullptr);
    m_data_size = std::exchange(other.m_data_size, 0);
    m_cursor = std::exchange(other.m_cursor, 0);
    m_pattern = std::exchange(other.m_pattern, access_pattern::normal);
    m_has_external_source = std::exchange(other.m_has_external_source, false);
    return *this;
}

std::expected<void, const char*> reader::open(const std::filesystem::path& path) noexcept {
    return open(path, access_pattern::sequential);
}

std::expected<void, const char*> reader::open(
    const std::filesystem::path& path,
    access_pattern pattern) noexcept
{
    if (is_open()) return std::unexpected("I/O reader failed: already open");
    return open_file_impl(path, pattern);
}

std::expected<void, const char*> reader::open(const uint8_t* data, size_t size) noexcept {
    if (is_open()) return std::unexpected("I/O reader failed: already open");
    if (data == nullptr && size != 0) {
        return std::unexpected("I/O reader failed: invalid memory buffer");
    }

    m_data_ptr = data;
    m_data_size = size;
    m_cursor = 0;
    m_pattern = access_pattern::normal;
    m_has_external_source = true;
    return {};
}

void reader::close() noexcept {
#if defined(_WIN32)
    if (m_handles && m_data_ptr != nullptr) UnmapViewOfFile(m_data_ptr);
    if (m_handles) {
        if (m_handles->mapping != nullptr) CloseHandle(m_handles->mapping);
        if (m_handles->file != INVALID_HANDLE_VALUE) CloseHandle(m_handles->file);
        m_handles.reset();
    }
#elif !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
    if (m_descriptor && m_data_ptr != nullptr) {
        munmap(const_cast<uint8_t*>(m_data_ptr), m_data_size);
    }
    if (m_descriptor && m_descriptor->fd >= 0) ::close(m_descriptor->fd);
    m_descriptor.reset();
#else
    m_fallback.reset();
#endif
    m_data_ptr = nullptr;
    m_data_size = 0;
    m_cursor = 0;
    m_pattern = access_pattern::normal;
    m_has_external_source = false;
}

bool reader::is_open() const noexcept {
    if (m_has_external_source) return true;
#if defined(_WIN32)
    return m_handles && m_handles->file != INVALID_HANDLE_VALUE;
#elif !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
    return m_descriptor && m_descriptor->fd >= 0;
#else
    return m_fallback != nullptr;
#endif
}

std::span<const uint8_t> reader::data() const noexcept { return {m_data_ptr, m_data_size}; }

size_t reader::size() const noexcept { return m_data_size; }

std::expected<void, const char*> reader::open_file_impl(
    const std::filesystem::path& path,
    access_pattern pattern) noexcept
{
#if defined(_WIN32)
    auto handles = std::make_unique<detail::win32_reader_handles>();
    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    if (pattern == access_pattern::random) flags |= FILE_FLAG_RANDOM_ACCESS;
    if (pattern == access_pattern::sequential) flags |= FILE_FLAG_SEQUENTIAL_SCAN;

    handles->file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, flags, nullptr);
    if (handles->file == INVALID_HANDLE_VALUE) {
        return std::unexpected("I/O reader failed: could not open file");
    }

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(handles->file, &file_size)) {
        CloseHandle(handles->file);
        return std::unexpected("I/O reader failed: could not get file size");
    }
    m_data_size = static_cast<size_t>(file_size.QuadPart);
    if (m_data_size != 0) {
        handles->mapping = CreateFileMappingW(handles->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (handles->mapping == nullptr) {
            CloseHandle(handles->file);
            return std::unexpected("I/O reader failed: could not create file mapping");
        }
        const void* mapped = MapViewOfFile(handles->mapping, FILE_MAP_READ, 0, 0, 0);
        if (mapped == nullptr) {
            CloseHandle(handles->mapping);
            CloseHandle(handles->file);
            return std::unexpected("I/O reader failed: could not map view of file");
        }
        m_data_ptr = static_cast<const uint8_t*>(mapped);
    }
    m_handles = std::move(handles);

#elif !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
    auto descriptor = std::make_unique<detail::posix_reader_descriptor>();
    descriptor->fd = ::open(path.c_str(), O_RDONLY);
    if (descriptor->fd < 0) {
        return std::unexpected("I/O reader failed: could not open file");
    }

    struct stat file_stats {};
    if (fstat(descriptor->fd, &file_stats) < 0) {
        ::close(descriptor->fd);
        return std::unexpected("I/O reader failed: could not stat file");
    }
    m_data_size = static_cast<size_t>(file_stats.st_size);
    if (m_data_size != 0) {
        void* mapped = mmap(nullptr, m_data_size, PROT_READ, MAP_PRIVATE, descriptor->fd, 0);
        if (mapped == MAP_FAILED) {
            ::close(descriptor->fd);
            return std::unexpected("I/O reader failed: could not memory-map file");
        }
#if defined(POSIX_FADV_SEQUENTIAL) && defined(POSIX_FADV_RANDOM)
        if (pattern == access_pattern::random) posix_fadvise(descriptor->fd, 0, 0, POSIX_FADV_RANDOM);
        if (pattern == access_pattern::sequential) posix_fadvise(descriptor->fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
#if defined(MADV_SEQUENTIAL) && defined(MADV_RANDOM)
        if (pattern == access_pattern::random) madvise(mapped, m_data_size, MADV_RANDOM);
        if (pattern == access_pattern::sequential) madvise(mapped, m_data_size, MADV_SEQUENTIAL);
#endif
        m_data_ptr = static_cast<const uint8_t*>(mapped);
    }
    m_descriptor = std::move(descriptor);

#else
#if defined(_WIN32)
    std::FILE* file = _wfopen(path.c_str(), L"rb");
#else
    std::FILE* file = std::fopen(path.c_str(), "rb");
#endif
    if (file == nullptr) return std::unexpected("I/O reader failed: could not open file");
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return std::unexpected("I/O reader failed: could not seek file");
    }
    const long file_size = std::ftell(file);
    if (file_size < 0) {
        std::fclose(file);
        return std::unexpected("I/O reader failed: could not get file size");
    }

    m_data_size = static_cast<size_t>(file_size);
    auto source = std::make_unique<detail::fallback_reader_state>();
    if (m_data_size != 0) {
        std::rewind(file);
        source->data = static_cast<uint8_t*>(std::malloc(m_data_size));
        if (source->data == nullptr) {
            std::fclose(file);
            return std::unexpected("I/O reader failed: could not allocate memory");
        }
        size_t read = 0;
        while (read < m_data_size) {
            const size_t count = std::fread(source->data + read, 1, m_data_size - read, file);
            if (count == 0) {
                std::fclose(file);
                return std::unexpected("I/O reader failed: could not read file");
            }
            read += count;
        }
        m_data_ptr = source->data;
    }
    std::fclose(file);
    m_fallback = std::move(source);
#endif

    m_cursor = 0;
    m_pattern = pattern;
    m_has_external_source = false;
    return {};
}

} // namespace cricodecs::io
