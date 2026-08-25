#pragma once
/**
 * @file wav_container.hpp
 * @brief RIFF/WAVE container API for PCM interchange.
 *
 * WAV handling is support code for CriCodecs bindings and codec wrappers.
 * Public API by Youjose.
 */

#include <vector>
#include <string>
#include <expected>
#include <span>
#include <filesystem>
#include <optional>
#include "../utilities/io.hpp"

namespace cricodecs::wav {

    struct GUID {
        uint32_t Data1;
        uint16_t Data2;
        uint16_t Data3;
        uint64_t Data4;
    };

    struct RiffHeader {
        uint32_t signature;
        uint32_t size;
        uint32_t format;
    };

    struct ChunkHeader {
        uint32_t signature;
        uint32_t size;
    };

    struct WavFormatHeader {
        uint16_t compression_mode;
        uint16_t channels;
        uint32_t sample_rate;
        uint32_t avg_bytes_per_sec;
        uint16_t block_align;
        uint16_t bit_depth;
    };

    struct WavFormatExtension {
        uint16_t extension_size;
        uint16_t valid_bits_per_sample;
        uint32_t channel_mask;
        GUID sub_format;
    };

    struct SampleLoop {
        uint32_t cue_point_id;
        uint32_t type;
        uint32_t start;
        uint32_t end;
        uint32_t fraction;
        uint32_t play_count;
    };

    struct CuePoint {
        uint32_t name;
        uint32_t position;
        uint32_t chunk_id;
        uint32_t chunk_start;
        uint32_t block_start;
        uint32_t sample_offset;
    };

    struct SamplerHeader {
        uint32_t manufacturer;
        uint32_t product;
        uint32_t sample_period;
        uint32_t midi_unity_note;
        uint32_t midi_pitch_fraction;
        uint32_t smpte_format;
        uint32_t smpte_offset;
    };

    struct SamplerChunkHeader {
        SamplerHeader sampler;
        uint32_t loop_count;
        uint32_t sampler_data_size;
    };

    struct SamplerChunk : SamplerHeader {
        std::vector<SampleLoop> loops;
        std::vector<uint8_t> sampler_data;
    };

    struct WavFormat : WavFormatHeader, WavFormatExtension {};

    class WavContainer {
    public:
        WavContainer() = default;

        std::expected<void, std::string> load(const std::string& path);
        std::expected<void, std::string> load(const std::filesystem::path& path);
        std::expected<void, std::string> load(std::span<const uint8_t> data);
        std::expected<void, std::string> load(std::vector<uint8_t>&& data);

        const WavFormat& format() const { return m_format; }
        const SamplerChunk& sampler() const { return m_sampler; }
        const std::vector<CuePoint>& cues() const { return m_cues; }
        const std::filesystem::path& source_path() const noexcept { return m_source_path; }
        bool has_loops() const { return !m_sampler.loops.empty(); }
        size_t sample_count() const { return m_format.block_align ? m_pcm_size / m_format.block_align : 0; }
        size_t channels() const { return m_format.channels; }
        uint32_t sample_rate() const { return m_format.sample_rate; }
        
        std::expected<std::span<const int16_t>, std::string> get_pcm16() const;
        std::expected<int16_t, std::string> get_sample(size_t index) const;

        static std::expected<void, std::string> write(
            const std::string& path,
            std::span<const int16_t> pcm_data,
            uint32_t sample_rate,
            uint16_t channels,
            std::span<const SampleLoop> loops = {}
        );

        static std::expected<std::vector<uint8_t>, std::string> build_bytes(
            std::span<const int16_t> pcm_data,
            uint32_t sample_rate,
            uint16_t channels,
            std::span<const SampleLoop> loops = {}
        );

        static std::expected<size_t, std::string> built_size(
            std::span<const int16_t> pcm_data,
            uint32_t sample_rate,
            uint16_t channels,
            std::span<const SampleLoop> loops = {}
        );

        static std::expected<void, std::string> build_into(
            std::span<uint8_t> output,
            std::span<const int16_t> pcm_data,
            uint32_t sample_rate,
            uint16_t channels,
            std::span<const SampleLoop> loops = {}
        );

        static std::expected<void, std::string> write(
            const std::string& path,
            const std::vector<int16_t>& pcm_data,
            uint32_t sample_rate,
            uint16_t channels,
            const std::vector<SampleLoop>& loops = {}
        ) {
            return write(path, std::span<const int16_t>(pcm_data), sample_rate, channels, 
                        std::span<const SampleLoop>(loops));
        }

    private:
        io::SourceView m_source;
        std::filesystem::path m_source_path;
        size_t m_pcm_offset = 0;
        size_t m_pcm_size = 0;

        WavFormat m_format{};
        SamplerChunk m_sampler{};
        std::vector<CuePoint> m_cues;

        mutable std::optional<std::vector<int16_t>> m_pcm16_cache;

        std::expected<void, std::string> parse_headers();
    };

}
