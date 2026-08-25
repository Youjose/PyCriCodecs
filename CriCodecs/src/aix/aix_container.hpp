#pragma once
/**
 * @file aix_container.hpp
 * @brief AIX segmented/layered ADX container API.
 *
 * Reader and builder behavior are grounded in official `aixmux` evidence.
 * The C++23 object surface is CriCodecs work by Youjose.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../utilities/io.hpp"

namespace cricodecs::aix {

using AixError = std::string;

namespace detail {

struct AixHeader {
    uint32_t magic;
    uint32_t data_size;
    uint32_t version;
    uint32_t header_size;
    uint8_t reserved_10[8];
    uint16_t segment_count;
    uint8_t reserved_1a[6];
};

} // namespace detail

struct AixSegment {
    uint32_t offset = 0;
    uint32_t size = 0;
    int32_t sample_count = 0;
    int32_t sample_rate = 0;
};

struct AixLayer {
    uint32_t sample_rate = 0;
    uint32_t channel_count = 0;
};

struct AixLoopInfo {
    size_t start_segment = 0;
    size_t end_segment = 0;
    uint64_t start_sample = 0;
    uint64_t end_sample = 0;
};

struct AixBuildSegment {
    std::vector<std::vector<uint8_t>> layer_adx_data;
};

class Aix {
public:
    std::expected<void, AixError> load(const std::filesystem::path& path);
    std::expected<void, AixError> load(std::span<const uint8_t> data);
    std::expected<void, AixError> load(std::vector<uint8_t>&& data);

    [[nodiscard]] static std::expected<std::vector<uint8_t>, AixError> build(
        std::span<const AixBuildSegment> segments
    );
    [[nodiscard]] static std::expected<void, AixError> build_to_file(
        std::span<const AixBuildSegment> segments,
        const std::filesystem::path& output_path
    );

    [[nodiscard]] std::expected<std::vector<uint8_t>, AixError> save() const;
    [[nodiscard]] std::expected<void, AixError> save_to_file(
        const std::filesystem::path& output_path
    ) const;

    [[nodiscard]] std::expected<void, AixError> add_segment(AixBuildSegment segment);
    [[nodiscard]] std::expected<void, AixError> replace_segment(
        size_t segment_index,
        AixBuildSegment segment
    );
    [[nodiscard]] std::expected<void, AixError> remove_segment(size_t segment_index);
    [[nodiscard]] std::expected<void, AixError> move_segment(size_t from_index, size_t to_index);
    [[nodiscard]] std::expected<void, AixError> add_layer(
        std::vector<std::vector<uint8_t>> segment_adx_data
    );
    [[nodiscard]] std::expected<void, AixError> replace_layer(
        size_t segment_index,
        size_t layer_index,
        std::span<const uint8_t> adx_data
    );
    [[nodiscard]] std::expected<void, AixError> remove_layer(size_t layer_index);
    [[nodiscard]] std::expected<void, AixError> move_layer(size_t from_index, size_t to_index);

    std::expected<std::vector<uint8_t>, AixError> segment_bytes(
        size_t segment_index,
        size_t layer_index
    ) const;
    std::expected<void, AixError> extract_file(
        size_t segment_index,
        size_t layer_index,
        const std::filesystem::path& output_path
    ) const;
    std::expected<void, AixError> extract(const std::filesystem::path& output_dir) const {
        return extract_all(output_dir);
    }
    std::expected<void, AixError> extract_all(const std::filesystem::path& output_dir) const;
    std::expected<std::vector<uint8_t>, AixError> extract_layer_segment(
        size_t segment_index,
        size_t layer_index
    ) const {
        return segment_bytes(segment_index, layer_index);
    }

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] const std::vector<AixSegment>& segments() const noexcept { return m_segments; }
    [[nodiscard]] const std::vector<AixLayer>& layers() const noexcept { return m_layers; }
    [[nodiscard]] uint64_t total_sample_count() const noexcept;
    [[nodiscard]] const std::optional<AixLoopInfo>& inferred_loop() const noexcept { return m_inferred_loop; }

private:
    struct AixPayload {
        uint32_t offset = 0;
        uint16_t size = 0;
        uint8_t layer = 0;
    };

    static constexpr size_t max_segments = 120;

    std::filesystem::path m_source_path;
    io::SourceView m_source;
    std::vector<AixSegment> m_segments;
    std::vector<AixLayer> m_layers;
    std::vector<std::vector<AixPayload>> m_segment_payloads;
    std::optional<AixLoopInfo> m_inferred_loop;

    std::expected<void, AixError> parse();
    std::expected<std::vector<std::span<const uint8_t>>, AixError> layer_payloads(
        size_t segment_index,
        size_t layer_index
    ) const;
    [[nodiscard]] std::expected<std::vector<AixBuildSegment>, AixError> build_segments() const;
    [[nodiscard]] std::expected<void, AixError> replace_segments(std::vector<AixBuildSegment> segments);

    template<class Edit>
    std::expected<void, AixError> edit_segments(Edit edit) {
        auto segments = build_segments();
        if (!segments) {
            return std::unexpected(segments.error());
        }
        if (auto result = edit(*segments); !result) {
            return result;
        }
        return replace_segments(std::move(*segments));
    }
};

using AixReader = Aix;

} // namespace cricodecs::aix
