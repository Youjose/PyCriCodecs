/**
 * @file wav_container.cpp
 * @brief RIFF/WAVE reader and writer helpers.
 *
 * WAV handling is support code for PCM interchange in CriCodecs bindings and
 * codec wrappers. Implemented by Youjose.
 */

#include "wav_container.hpp"
#include "../utilities/numeric.hpp"

#include <bit>
#include <limits>
#include <cmath>
#include <cstring>
#include <utility>

namespace cricodecs::wav {

    static constexpr uint32_t RIFF_MAGIC = 0x46464952;
    static constexpr uint32_t WAVE_MAGIC = 0x45564157;
    static constexpr uint32_t FMT_MAGIC  = 0x20746D66;
    static constexpr uint32_t DATA_MAGIC = 0x61746164;
    static constexpr uint32_t SMPL_MAGIC = 0x6C706D73;
    static constexpr uint32_t CUE_MAGIC  = 0x20657563;

    static constexpr uint16_t WAVE_FORMAT_PCM        = 0x0001;
    static constexpr uint16_t WAVE_FORMAT_IEEE_FLOAT = 0x0003;
    static constexpr uint16_t WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

    [[nodiscard]] constexpr int16_t float_to_pcm16(double sample) noexcept {
        if (!std::isfinite(sample)) {
            return 0;
        }
        if (sample <= -1.0) {
            return std::numeric_limits<int16_t>::lowest();
        }
        const double clamped = cricodecs::util::clamp(sample, -1.0, 1.0);
        return cricodecs::util::clamp_to<int16_t>(static_cast<int32_t>(clamped * 32767.0));
    }

    [[nodiscard]] constexpr int16_t pcm8_to_pcm16(uint8_t sample, uint16_t source_bits) noexcept {
        const int midpoint = source_bits < 8 ? (1 << (source_bits - 1)) : 0x80;
        return static_cast<int16_t>((static_cast<int>(sample) - midpoint) << 8);
    }

    [[nodiscard]] constexpr int16_t signed_pcm_to_pcm16(int32_t sample, uint16_t source_bits) noexcept {
        if (source_bits <= 16) {
            return cricodecs::util::clamp_to<int16_t>(sample);
        }
        return static_cast<int16_t>(sample >> (source_bits - 16));
    }

    struct WavWriteLayout {
        uint32_t data_size = 0;
        uint32_t smpl_size = 0;
        uint32_t riff_size = 0;
    };

    struct PcmFormat {
        uint16_t compression;
        uint16_t storage_bits;
        uint16_t valid_bits;
    };

    [[nodiscard]] PcmFormat pcm_format(const WavFormat& format) noexcept {
        if (format.compression_mode != WAVE_FORMAT_EXTENSIBLE) {
            return {format.compression_mode, format.bit_depth, format.bit_depth};
        }
        return {
            static_cast<uint16_t>(format.sub_format.Data1),
            format.bit_depth,
            format.valid_bits_per_sample,
        };
    }

    enum class PcmEncoding { unsigned_8, signed_16, signed_24, signed_32, float_32, float_64 };

    struct PcmDecoder {
        PcmEncoding encoding;
        size_t sample_bytes;
        uint16_t valid_bits;

        [[nodiscard]] int16_t operator()(const uint8_t* sample) const noexcept {
            switch (encoding) {
            case PcmEncoding::unsigned_8: return pcm8_to_pcm16(*sample, valid_bits);
            case PcmEncoding::signed_16: return io::read_le<int16_t>(sample);
            case PcmEncoding::signed_24: return signed_pcm_to_pcm16(io::read_le<io::Int24>(sample), valid_bits);
            case PcmEncoding::signed_32: return signed_pcm_to_pcm16(io::read_le<int32_t>(sample), valid_bits);
            case PcmEncoding::float_32: return float_to_pcm16(io::read_le<float>(sample));
            case PcmEncoding::float_64: return float_to_pcm16(io::read_le<double>(sample));
            }
            std::unreachable();
        }
    };

    [[nodiscard]] std::expected<PcmDecoder, std::string> make_pcm_decoder(PcmFormat format) {
        const size_t sample_bytes = util::divide_round_up(format.storage_bits, 8u);
        if (format.compression == WAVE_FORMAT_IEEE_FLOAT) {
            if (format.storage_bits == 32) {
                return PcmDecoder{PcmEncoding::float_32, sample_bytes, format.valid_bits};
            }
            if (format.storage_bits == 64) {
                return PcmDecoder{PcmEncoding::float_64, sample_bytes, format.valid_bits};
            }
        } else if (format.compression == WAVE_FORMAT_PCM) {
            if (format.valid_bits <= 8) {
                return PcmDecoder{PcmEncoding::unsigned_8, sample_bytes, format.valid_bits};
            }
            if (format.valid_bits <= 16 && sample_bytes == 2) {
                return PcmDecoder{PcmEncoding::signed_16, sample_bytes, format.valid_bits};
            }
            if (format.valid_bits <= 24 && sample_bytes == 3) {
                return PcmDecoder{PcmEncoding::signed_24, sample_bytes, format.valid_bits};
            }
            if (format.valid_bits <= 32 && sample_bytes == 4) {
                return PcmDecoder{PcmEncoding::signed_32, sample_bytes, format.valid_bits};
            }
        }
        return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
    }

    std::expected<WavWriteLayout, std::string> make_write_layout(
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        if (channels == 0 || sample_rate == 0) {
            return std::unexpected(std::string("WAV write failed: sample rate and channel count must be non-zero"));
        }
        if (channels > std::numeric_limits<uint16_t>::max() / sizeof(int16_t)) {
            return std::unexpected(std::string("WAV write failed: channel count is too large"));
        }
        if (pcm_data.size() > std::numeric_limits<uint32_t>::max() / sizeof(int16_t)) {
            return std::unexpected(std::string("WAV write failed: PCM data is too large for RIFF WAVE"));
        }

        WavWriteLayout layout;
        layout.data_size = static_cast<uint32_t>(pcm_data.size() * sizeof(int16_t));
        const auto block_align = static_cast<uint16_t>(channels * sizeof(int16_t));
        if (sample_rate > std::numeric_limits<uint32_t>::max() / block_align) {
            return std::unexpected(std::string("WAV write failed: byte rate is too large"));
        }

        if (!loops.empty()) {
            if (loops.size() >
                (std::numeric_limits<uint32_t>::max() - sizeof(SamplerChunkHeader)) / sizeof(SampleLoop)) {
                return std::unexpected(std::string("WAV write failed: too many sample loops"));
            }
            layout.smpl_size = static_cast<uint32_t>(
                sizeof(SamplerChunkHeader) + loops.size() * sizeof(SampleLoop));
        }

        uint64_t riff_size = sizeof(uint32_t) +
            (sizeof(ChunkHeader) + sizeof(WavFormatHeader)) +
            (sizeof(ChunkHeader) + layout.data_size);
        if (!loops.empty()) {
            riff_size += sizeof(ChunkHeader) + layout.smpl_size;
        }
        if (riff_size > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected(std::string("WAV write failed: PCM data is too large for RIFF WAVE"));
        }
        layout.riff_size = static_cast<uint32_t>(riff_size);

        return layout;
    }

    uint8_t* emit_wave_header(
        uint8_t* output,
        const WavWriteLayout& layout,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        output = io::write_le(output, RiffHeader{RIFF_MAGIC, layout.riff_size, WAVE_MAGIC});
        const auto block_align = static_cast<uint16_t>(channels * sizeof(int16_t));
        output = io::write_le(output, ChunkHeader{FMT_MAGIC, static_cast<uint32_t>(sizeof(WavFormatHeader))});
        output = io::write_le(output, WavFormatHeader{
            WAVE_FORMAT_PCM, channels, sample_rate, sample_rate * block_align, block_align, 16,
        });

        if (!loops.empty()) {
            output = io::write_le(output, ChunkHeader{SMPL_MAGIC, layout.smpl_size});
            output = io::write_le(output, SamplerChunkHeader{
                {0, 0, 1000000000 / sample_rate, 60, 0, 0, 0},
                static_cast<uint32_t>(loops.size()), 0,
            });
            output = io::write_le(output, loops);
        }

        return io::write_le(output, ChunkHeader{DATA_MAGIC, layout.data_size});
    }

    void emit_wave(
        std::span<uint8_t> output,
        const WavWriteLayout& layout,
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        auto* position = emit_wave_header(output.data(), layout, sample_rate, channels, loops);
        if (!pcm_data.empty()) {
            std::memcpy(position, pcm_data.data(), pcm_data.size_bytes());
        }
    }

    template <typename Convert>
    std::expected<void, std::string> convert_pcm_payload(
        std::span<const uint8_t> source,
        std::span<int16_t> target,
        size_t frame_count,
        uint16_t channels,
        uint16_t block_align,
        size_t sample_bytes,
        Convert convert)
    {
        if (sample_bytes == 0 || channels == 0) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }
        if (frame_count > std::numeric_limits<size_t>::max() / channels) {
            return std::unexpected(std::string("WAV PCM data is too large"));
        }
        if (target.size() != frame_count * channels) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }

        const size_t tight_block_align = static_cast<size_t>(channels) * sample_bytes;
        if (block_align == tight_block_align) {
            const size_t bytes_needed = target.size() * sample_bytes;
            if (bytes_needed > source.size()) {
                return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
            }

            const uint8_t* src = source.data();
            for (int16_t& sample : target) {
                sample = convert(src);
                src += sample_bytes;
            }
            return {};
        }

        if (frame_count > std::numeric_limits<size_t>::max() / block_align) {
            return std::unexpected(std::string("WAV PCM data is too large"));
        }
        const size_t bytes_needed = frame_count * static_cast<size_t>(block_align);
        if (bytes_needed > source.size() || block_align < tight_block_align) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }

        size_t out = 0;
        const uint8_t* frame = source.data();
        for (size_t i = 0; i < frame_count; ++i) {
            const uint8_t* src = frame;
            for (uint16_t ch = 0; ch < channels; ++ch) {
                target[out++] = convert(src);
                src += sample_bytes;
            }
            frame += block_align;
        }
        return {};
    }

    std::expected<void, std::string> decode_pcm16(
        std::span<const uint8_t> source,
        std::span<int16_t> target,
        size_t frame_count,
        uint16_t channels,
        uint16_t block_align,
        PcmFormat format)
    {
        auto decoder = make_pcm_decoder(format);
        if (!decoder) return std::unexpected(decoder.error());

        const bool tight_frames = block_align == static_cast<size_t>(channels) * decoder->sample_bytes;
        if (format.compression == WAVE_FORMAT_PCM && format.valid_bits > 8 && format.valid_bits <= 16 &&
            decoder->sample_bytes == sizeof(int16_t) && tight_frames) {
            const size_t bytes_needed = target.size() * sizeof(int16_t);
            if (bytes_needed > source.size()) {
                return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
            }
            if constexpr (std::endian::native == std::endian::little) {
                std::memcpy(target.data(), source.data(), bytes_needed);
            } else {
                const auto result = convert_pcm_payload(
                    source, target, frame_count, channels, block_align, decoder->sample_bytes, *decoder);
                if (!result) return result;
            }
            return {};
        }

        switch (decoder->encoding) {
        case PcmEncoding::unsigned_8:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 1,
                [bits = decoder->valid_bits](const uint8_t* src) { return pcm8_to_pcm16(*src, bits); });
        case PcmEncoding::signed_16:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 2,
                [](const uint8_t* src) { return io::read_le<int16_t>(src); });
        case PcmEncoding::signed_24:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 3,
                [bits = decoder->valid_bits](const uint8_t* src) {
                    return signed_pcm_to_pcm16(io::read_le<io::Int24>(src), bits);
                });
        case PcmEncoding::signed_32:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 4,
                [bits = decoder->valid_bits](const uint8_t* src) {
                    return signed_pcm_to_pcm16(io::read_le<int32_t>(src), bits);
                });
        case PcmEncoding::float_32:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 4,
                [](const uint8_t* src) { return float_to_pcm16(io::read_le<float>(src)); });
        case PcmEncoding::float_64:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 8,
                [](const uint8_t* src) { return float_to_pcm16(io::read_le<double>(src)); });
        }
        std::unreachable();
    }

    std::expected<void, std::string> WavContainer::load(const std::string& path) {
        return load(std::filesystem::path(path));
    }

    std::expected<void, std::string> WavContainer::load(const std::filesystem::path& path) {
        auto source = io::SourceView::from_file(path);
        if (!source) {
            return std::unexpected(
                "WAV load failed: failed to open " + path.string() + " (" + source.error() + ")");
        }
        m_source = std::move(*source);
        m_source_path.clear();
        if (auto result = parse_headers(); !result) return result;
        m_source_path = path;
        return {};
    }

    std::expected<void, std::string> WavContainer::load(std::vector<uint8_t>&& data) {
        m_source_path.clear();
        m_source = io::SourceView::from_owned(std::move(data));
        return parse_headers();
    }

    std::expected<void, std::string> WavContainer::load(std::span<const uint8_t> data) {
        return load(std::vector<uint8_t>(data.begin(), data.end()));
    }

    std::expected<void, std::string> WavContainer::parse_headers() {
        m_pcm_offset = 0;
        m_pcm_size = 0;
        m_format = {};
        m_sampler = {};
        m_cues.clear();
        m_pcm16_cache.reset();

        io::reader reader;
        if (!reader.open(m_source) || reader.size() < 12) {
            return std::unexpected(std::string("WAV parse failed: invalid RIFF/WAVE header"));
        }
        
        const auto riff = reader.read_le<RiffHeader>();
        if (riff.signature != RIFF_MAGIC || riff.format != WAVE_MAGIC) {
            return std::unexpected(std::string("WAV parse failed: invalid RIFF/WAVE header"));
        }

        size_t sum_size = 4;
        bool has_fmt = false;

        while (sum_size < riff.size && reader.remaining() >= sizeof(ChunkHeader)) {
            const size_t chunk_start = reader.tell();
            const auto chunk = reader.read_le<ChunkHeader>();
            
            const size_t data_offset = reader.tell();
            size_t total_chunk_size = static_cast<size_t>(chunk.size) + sizeof(ChunkHeader);
            
            if ((chunk.size & 1) && (total_chunk_size + sum_size + 1 <= riff.size)) {
                total_chunk_size += 1;
            }

            if (chunk.size > reader.remaining()) {
                return std::unexpected(std::string("WAV I/O failed"));
            }

            switch (chunk.signature) {
            case FMT_MAGIC: {
                if (chunk.size < sizeof(WavFormatHeader)) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                static_cast<WavFormatHeader&>(m_format) = reader.read_le<WavFormatHeader>();
                
                if (m_format.compression_mode == WAVE_FORMAT_EXTENSIBLE) {
                    if (chunk.size < sizeof(WavFormatHeader) + sizeof(WavFormatExtension)) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                    static_cast<WavFormatExtension&>(m_format) = reader.read_le<WavFormatExtension>();
                    if (m_format.extension_size < 22) return std::unexpected(std::string("WAV parse failed: invalid format data"));

                    if (m_format.sub_format.Data1 != WAVE_FORMAT_PCM && 
                        m_format.sub_format.Data1 != WAVE_FORMAT_EXTENSIBLE && 
                        m_format.sub_format.Data1 != WAVE_FORMAT_IEEE_FLOAT) {
                        return std::unexpected(std::string("WAV parse failed: unsupported compression mode"));
                    }
                }
                
                if (m_format.compression_mode != WAVE_FORMAT_PCM && 
                    m_format.compression_mode != WAVE_FORMAT_EXTENSIBLE && 
                    m_format.compression_mode != WAVE_FORMAT_IEEE_FLOAT) {
                    return std::unexpected(std::string("WAV parse failed: unsupported compression mode"));
                }
                has_fmt = true;
                break;
            }
            case SMPL_MAGIC: {
                if (chunk.size < sizeof(SamplerChunkHeader)) return std::unexpected(std::string("WAV parse failed: invalid smpl loop data"));
                const auto sampler = reader.read_le<SamplerChunkHeader>();
                static_cast<SamplerHeader&>(m_sampler) = sampler.sampler;
                
                const uint64_t expected_size = sizeof(SamplerChunkHeader) +
                    static_cast<uint64_t>(sampler.loop_count) * sizeof(SampleLoop) + sampler.sampler_data_size;
                if (chunk.size < expected_size) return std::unexpected(std::string("WAV parse failed: invalid smpl loop data"));

                m_sampler.loops.resize(sampler.loop_count);
                reader.read_le(std::span{m_sampler.loops});
                if (sampler.sampler_data_size > 0) {
                    auto data_span = reader.read_bytes(sampler.sampler_data_size);
                    m_sampler.sampler_data.assign(data_span.begin(), data_span.end());
                }
                break;
            }
            case DATA_MAGIC: {
                m_pcm_offset = data_offset;
                m_pcm_size = chunk.size;
                break;
            }
            case CUE_MAGIC: {
                if (chunk.size < 4) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                uint32_t num_cues = reader.read_le<uint32_t>();
                const uint64_t expected_size = sizeof(num_cues) + static_cast<uint64_t>(num_cues) * sizeof(CuePoint);
                if (chunk.size < expected_size) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                m_cues.resize(num_cues);
                reader.read_le(std::span{m_cues});
                break;
            }
            default:
                break;
            }

            sum_size += total_chunk_size;
            
            if (sum_size > riff.size) return std::unexpected(std::string("WAV parse failed: chunk table exceeds RIFF size"));
            
            reader.seek(chunk_start + total_chunk_size);
        }

        if (!has_fmt) return std::unexpected(std::string("WAV parse failed: invalid format data"));
        if (m_pcm_size == 0) return std::unexpected(std::string("WAV PCM data chunk is missing"));
        if (m_format.channels == 0 || m_format.sample_rate == 0 ||
            m_format.block_align == 0 || m_format.bit_depth == 0) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }

        // If we have cue points but no loops (smpl), convert cue points to a loop
        if (m_sampler.loops.empty() && !m_cues.empty()) {
            SampleLoop loop{};
            loop.cue_point_id = m_cues[0].name;
            loop.type = 0; // forward loop
            loop.start = m_cues[0].position;
            if (m_cues.size() >= 2) {
                loop.end = m_cues[1].position;
            } else {
                loop.end = static_cast<uint32_t>(sample_count());
            }
            loop.fraction = 0;
            loop.play_count = 0; // infinite
            m_sampler.loops.push_back(loop);
        }

        const auto pcm = pcm_format(m_format);
        const size_t storage_bytes = util::divide_round_up(pcm.storage_bits, 8u);
        if (storage_bytes == 0 || storage_bytes > 8 ||
            m_format.block_align < m_format.channels * storage_bytes ||
            pcm.valid_bits == 0) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }

        if (pcm.valid_bits > pcm.storage_bits) {
            return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
        }

        if (pcm.compression == WAVE_FORMAT_IEEE_FLOAT) {
            if (pcm.storage_bits != 32 && pcm.storage_bits != 64) {
                return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
            }
        } else if (pcm.compression == WAVE_FORMAT_PCM) {
            if (pcm.storage_bits > 32) {
                return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
            }
        } else {
            return std::unexpected(std::string("WAV parse failed: unsupported compression mode"));
        }

        return {};
    }

    std::expected<int16_t, std::string> WavContainer::get_sample(size_t index) const {
        if (m_pcm_size == 0) return std::unexpected(std::string("WAV PCM data chunk is missing"));
        if (m_pcm16_cache) {
            if (index >= m_pcm16_cache->size()) {
                return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
            }
            return (*m_pcm16_cache)[index];
        }

        const size_t frames = sample_count();
        if (m_format.channels == 0 || frames > std::numeric_limits<size_t>::max() / m_format.channels ||
            index >= frames * m_format.channels) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }

        const auto format = pcm_format(m_format);
        auto decoder = make_pcm_decoder(format);
        if (!decoder) return std::unexpected(decoder.error());

        const size_t offset = (index / m_format.channels) * m_format.block_align +
            (index % m_format.channels) * decoder->sample_bytes;
        const auto pcm = m_source.subspan(m_pcm_offset, m_pcm_size);
        if (offset > pcm.size() || decoder->sample_bytes > pcm.size() - offset) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }
        return (*decoder)(pcm.data() + offset);
    }
    
    std::expected<std::span<const int16_t>, std::string> WavContainer::get_pcm16() const {
        if (m_pcm_size == 0) return std::unexpected(std::string("WAV PCM data chunk is missing"));
        
        if (m_pcm16_cache) return std::span<const int16_t>(*m_pcm16_cache);
        
        if (m_format.channels == 0) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }
        const size_t frames = sample_count();
        if (frames > std::numeric_limits<size_t>::max() / m_format.channels) {
            return std::unexpected(std::string("WAV PCM data is too large"));
        }

        const size_t total_samples = frames * m_format.channels;
        const auto pcm_bytes = m_source.subspan(m_pcm_offset, m_pcm_size);

        m_pcm16_cache.emplace(total_samples);

        const auto pcm = pcm_format(m_format);
        auto convert_result = decode_pcm16(
            pcm_bytes,
            std::span<int16_t>(*m_pcm16_cache),
            frames,
            m_format.channels,
            m_format.block_align,
            pcm);
        if (!convert_result) {
            m_pcm16_cache.reset();
            return std::unexpected(convert_result.error());
        }

        return std::span<const int16_t>(*m_pcm16_cache);
    }

    std::expected<std::vector<uint8_t>, std::string> WavContainer::build_bytes(
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        auto layout = make_write_layout(pcm_data, sample_rate, channels, loops);
        if (!layout) {
            return std::unexpected(layout.error());
        }

        std::vector<uint8_t> output(static_cast<size_t>(layout->riff_size) + 8);
        emit_wave(output, *layout, pcm_data, sample_rate, channels, loops);
        return output;
    }

    std::expected<size_t, std::string> WavContainer::built_size(
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        auto layout = make_write_layout(pcm_data, sample_rate, channels, loops);
        if (!layout) {
            return std::unexpected(layout.error());
        }
        return static_cast<size_t>(layout->riff_size) + 8;
    }

    std::expected<void, std::string> WavContainer::build_into(
        std::span<uint8_t> output,
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        auto layout = make_write_layout(pcm_data, sample_rate, channels, loops);
        if (!layout) {
            return std::unexpected(layout.error());
        }
        const size_t required_size = static_cast<size_t>(layout->riff_size) + 8;
        if (output.size() != required_size) {
            return std::unexpected("WAV build failed: output span has the wrong size");
        }

        emit_wave(output, *layout, pcm_data, sample_rate, channels, loops);
        return {};
    }

    std::expected<void, std::string> WavContainer::write(
        const std::string& path,
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        return build_bytes(pcm_data, sample_rate, channels, loops).and_then([&](const auto& bytes) {
            return io::write_file_bytes(path, bytes, "WAV write failed");
        });
    }

}
