/**
 * @file sfd_container.cpp
 * @brief SofDec 1/SFD container object helpers.
 *
 * The public container surface is grounded in official tool behavior for the
 * reviewed SofdecStream subset. Implementation and validation by Youjose.
 */

#include "sfd_container.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

#include "../utilities/string.hpp"

namespace cricodecs::sfd {

namespace {

using util::lowercase_ascii;

} // namespace

std::filesystem::path SfdStream::suggested_path(bool include_index_prefix) const {
    const std::string_view kind = type == SfdStreamType::audio
        ? "audio"
        : type == SfdStreamType::video ? "video" : "private";
    const std::string stem = std::string(kind) +
        (include_index_prefix ? "_" + std::to_string(type_index) : "");

    if (type == SfdStreamType::audio) {
        return std::filesystem::path(stem + stream_extension(audio_type));
    }

    if (type == SfdStreamType::video) {
        constexpr std::array extensions = {
            std::string_view(".sfv"), std::string_view(".m1v"),
            std::string_view(".mpv"), std::string_view(".m2v")
        };
        if (element_record && element_record->source_type < extensions.size()) {
            return std::filesystem::path(stem + std::string(extensions[element_record->source_type]));
        }

        const std::string source_extension = lowercase_ascii(std::filesystem::path(source_name).extension().string());
        if (std::ranges::contains(extensions, source_extension)) {
            return std::filesystem::path(stem + source_extension);
        }

        return std::filesystem::path(stem + (video_type == SfdVideoType::mpeg1 ? ".m1v" : ".m2v"));
    }

    return std::filesystem::path(stem + ".bin");
}

std::expected<SfdContainer, std::string> SfdContainer::load(const std::filesystem::path& path) {
    SfdContainer container;
    auto source = io::SourceView::from_file(path);
    if (!source) {
        return std::unexpected("SFD load failed: could not open input: " + path.string());
    }

    container.m_source = std::move(*source);
    container.m_source_path = path;
    return container.parse().transform([&] { return std::move(container); });
}

std::expected<SfdContainer, std::string> SfdContainer::load(std::span<const uint8_t> data) {
    return load(std::vector<uint8_t>(data.begin(), data.end()));
}

std::expected<SfdContainer, std::string> SfdContainer::load(std::vector<uint8_t>&& data) {
    SfdContainer container;
    container.m_source = io::SourceView::from_owned(std::move(data));
    return container.parse().transform([&] { return std::move(container); });
}

const SfdStream* SfdContainer::find_stream_by_id(uint8_t stream_id) const noexcept {
    const auto it = std::find_if(m_streams.begin(), m_streams.end(), [stream_id](const SfdStream& stream) {
        return stream.stream_id == stream_id;
    });
    return it == m_streams.end() ? nullptr : &*it;
}

std::expected<std::map<std::string, std::vector<uint8_t>>, std::string> SfdContainer::demux(
    bool include_index_prefix
) const {
    std::map<std::string, std::vector<uint8_t>> streams;
    for (const auto& stream : m_streams) {
        auto bytes = extract_stream(stream.index);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        streams.emplace(stream.suggested_path(include_index_prefix).generic_string(), std::move(*bytes));
    }
    return streams;
}

std::expected<std::vector<uint8_t>, std::string> SfdContainer::extract_stream(uint32_t index) const {
    if (index >= m_streams.size()) {
        return std::unexpected("SFD stream index is out of range");
    }

    const auto& stream = m_streams[index];
    if (stream.extracted_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return std::unexpected("SFD stream is too large to materialize in memory");
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(stream.extracted_size));

    const auto source = m_source.bytes;
    for (const auto& chunk : stream.chunks) {
        const auto slice = source.subspan(static_cast<size_t>(chunk.source_offset), chunk.size);
        bytes.insert(bytes.end(), slice.begin(), slice.end());
    }

    return bytes;
}

std::expected<std::vector<uint8_t>, std::string> SfdContainer::save() const {
    const auto source = m_source.bytes;
    return std::vector<uint8_t>(source.begin(), source.end());
}

std::expected<void, std::string> SfdContainer::save_to_file(const std::filesystem::path& output_path) const {
    return io::write_file_bytes(output_path, m_source.bytes, "SFD save failed");
}

std::expected<void, std::string> SfdContainer::export_stream(
    uint32_t index,
    const std::filesystem::path& output_path
) const {
    if (index >= m_streams.size()) {
        return std::unexpected("SFD stream index is out of range");
    }

    if (output_path.has_parent_path()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("SFD export failed: could not create output directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("SFD export failed: could not open output: " + output_path.string());
    }

    const auto source = m_source.bytes;
    const auto& stream = m_streams[index];
    for (const auto& chunk : stream.chunks) {
        if (auto result = writer.write(source.subspan(static_cast<size_t>(chunk.source_offset), chunk.size)); !result) {
            (void)writer.close();
            return std::unexpected("SFD export failed: could not write output: " + output_path.string());
        }
    }

    if (auto result = writer.close(); !result) {
        return std::unexpected("SFD export failed: could not finalize output: " + output_path.string());
    }

    return {};
}

std::expected<void, std::string> SfdContainer::export_all(const std::filesystem::path& output_dir) const {
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_dir, filesystem_error);
    if (filesystem_error) {
        return std::unexpected("SFD export failed: could not create output directory: " + filesystem_error.message());
    }

    for (const auto& stream : m_streams) {
        auto export_result = export_stream(stream.index, output_dir / stream.suggested_path());
        if (!export_result) {
            return std::unexpected(export_result.error());
        }
    }

    return {};
}

} // namespace cricodecs::sfd
