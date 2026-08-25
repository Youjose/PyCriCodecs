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
#include <memory>
#include <type_traits>
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
    static constexpr uint32_t PCM_FORMAT_SIZE = 16;

    [[nodiscard]] constexpr size_t storage_bytes_for_bits(uint16_t bits) noexcept {
        return (static_cast<size_t>(bits) + 7) / 8;
    }

    [[nodiscard]] constexpr bool checked_range(size_t offset, size_t size, size_t limit) noexcept {
        return offset <= limit && size <= limit - offset;
    }

    [[nodiscard]] std::span<const uint8_t> bounded_subspan(
        std::span<const uint8_t> bytes, size_t offset, size_t size) noexcept
    {
        return checked_range(offset, size, bytes.size()) ? bytes.subspan(offset, size) : std::span<const uint8_t>{};
    }

    template <typename T>
    [[nodiscard]] T read_le_unaligned(const uint8_t* data) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            return std::bit_cast<float>(io::read_le<uint32_t>(data));
        } else if constexpr (std::is_same_v<T, double>) {
            return std::bit_cast<double>(io::read_le<uint64_t>(data));
        } else {
            return io::read_le<T>(data);
        }
    }

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
            case PcmEncoding::signed_16: return read_le_unaligned<int16_t>(sample);
            case PcmEncoding::signed_24: return signed_pcm_to_pcm16(read_le_unaligned<io::Int24>(sample), valid_bits);
            case PcmEncoding::signed_32: return signed_pcm_to_pcm16(read_le_unaligned<int32_t>(sample), valid_bits);
            case PcmEncoding::float_32: return float_to_pcm16(read_le_unaligned<float>(sample));
            case PcmEncoding::float_64: return float_to_pcm16(read_le_unaligned<double>(sample));
            }
            std::unreachable();
        }
    };

    [[nodiscard]] std::expected<PcmDecoder, std::string> make_pcm_decoder(PcmFormat format) {
        const size_t sample_bytes = storage_bytes_for_bits(format.storage_bits);
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
            if (loops.size() > (std::numeric_limits<uint32_t>::max() - 36) / 24) {
                return std::unexpected(std::string("WAV write failed: too many sample loops"));
            }
            layout.smpl_size = 36 + static_cast<uint32_t>(loops.size()) * 24;
        }

        uint64_t riff_size = 4ull + (8ull + PCM_FORMAT_SIZE) + (8ull + layout.data_size);
        if (!loops.empty()) {
            riff_size += 8ull + layout.smpl_size;
        }
        if (riff_size > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected(std::string("WAV write failed: PCM data is too large for RIFF WAVE"));
        }
        layout.riff_size = static_cast<uint32_t>(riff_size);

        return layout;
    }

    template <typename WriteU16, typename WriteU32>
    void emit_wave_header(
        WriteU16&& write_u16,
        WriteU32&& write_u32,
        const WavWriteLayout& layout,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        write_u32(RIFF_MAGIC);
        write_u32(layout.riff_size);
        write_u32(WAVE_MAGIC);

        write_u32(FMT_MAGIC);
        const auto block_align = static_cast<uint16_t>(channels * sizeof(int16_t));
        write_u32(PCM_FORMAT_SIZE);
        write_u16(WAVE_FORMAT_PCM);
        write_u16(channels);
        write_u32(sample_rate);
        write_u32(sample_rate * block_align);
        write_u16(block_align);
        write_u16(16);

        if (!loops.empty()) {
            write_u32(SMPL_MAGIC);
            write_u32(layout.smpl_size);

            write_u32(0);
            write_u32(0);
            write_u32(sample_rate > 0 ? 1000000000 / sample_rate : 0);
            write_u32(60);
            write_u32(0);
            write_u32(0);
            write_u32(0);
            write_u32(static_cast<uint32_t>(loops.size()));
            write_u32(0);

            for (const auto& loop : loops) {
                write_u32(loop.cue_point_id);
                write_u32(loop.type);
                write_u32(loop.start);
                write_u32(loop.end);
                write_u32(loop.fraction);
                write_u32(loop.play_count);
            }
        }

        write_u32(DATA_MAGIC);
        write_u32(layout.data_size);
    }

    void emit_wave(
        std::span<uint8_t> output,
        const WavWriteLayout& layout,
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        size_t offset = 0;
        emit_wave_header(
            [&](uint16_t value) {
                io::write_le<uint16_t>(output.data() + offset, value);
                offset += sizeof(value);
            },
            [&](uint32_t value) {
                io::write_le<uint32_t>(output.data() + offset, value);
                offset += sizeof(value);
            },
            layout, sample_rate, channels, loops);
        if (!pcm_data.empty()) {
            std::memcpy(output.data() + offset, pcm_data.data(), pcm_data.size_bytes());
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
                [](const uint8_t* src) { return read_le_unaligned<int16_t>(src); });
        case PcmEncoding::signed_24:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 3,
                [bits = decoder->valid_bits](const uint8_t* src) {
                    return signed_pcm_to_pcm16(read_le_unaligned<io::Int24>(src), bits);
                });
        case PcmEncoding::signed_32:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 4,
                [bits = decoder->valid_bits](const uint8_t* src) {
                    return signed_pcm_to_pcm16(read_le_unaligned<int32_t>(src), bits);
                });
        case PcmEncoding::float_32:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 4,
                [](const uint8_t* src) { return float_to_pcm16(read_le_unaligned<float>(src)); });
        case PcmEncoding::float_64:
            return convert_pcm_payload(source, target, frame_count, channels, block_align, 8,
                [](const uint8_t* src) { return float_to_pcm16(read_le_unaligned<double>(src)); });
        }
        std::unreachable();
    }

    std::expected<void, std::string> WavContainer::load(const std::string& path) {
        return load(std::filesystem::path(path));
    }

    std::expected<void, std::string> WavContainer::load(const std::filesystem::path& path) {
        auto bytes = io::read_file_bytes(path, "WAV load failed");
        if (!bytes) {
            return std::unexpected(bytes.error());
        }

        auto res = load(std::move(*bytes));
        if (!res) {
            return res;
        }
        m_source_path = path;
        return {};
    }

    std::expected<void, std::string> WavContainer::load(std::vector<uint8_t>&& data) {
        m_source_path.clear();
        auto owner = std::make_shared<std::vector<uint8_t>>(std::move(data));
        const std::span<const uint8_t> bytes(*owner);
        m_source = io::SourceView(bytes, std::move(owner));
        return parse_headers();
    }

    std::expected<void, std::string> WavContainer::load(std::span<const uint8_t> data) {
        return load(std::vector<uint8_t>(data.begin(), data.end()));
    }

    std::expected<void, std::string> WavContainer::parse_headers() {
        io::reader reader;
        if (!reader.open(m_source) || reader.size() < 12) {
            return std::unexpected(std::string("WAV parse failed: invalid RIFF/WAVE header"));
        }

        m_pcm_offset = 0;
        m_pcm_size = 0;
        m_format = {};
        m_sampler = {};
        m_cues.clear();
        m_pcm16_cache.reset();
        
        if (reader.read_le<uint32_t>() != RIFF_MAGIC) return std::unexpected(std::string("WAV parse failed: invalid RIFF/WAVE header"));
        const uint32_t full_size = reader.read_le<uint32_t>();
        if (reader.read_le<uint32_t>() != WAVE_MAGIC) return std::unexpected(std::string("WAV parse failed: invalid RIFF/WAVE header"));

        size_t sum_size = 4;
        bool has_fmt = false;

        while (sum_size < full_size && reader.remaining() >= 8) {
            const size_t chunk_start = reader.tell();
            const uint32_t sig = reader.read_le<uint32_t>();
            const uint32_t size = reader.read_le<uint32_t>();
            
            const size_t data_offset = reader.tell();
            size_t total_chunk_size = static_cast<size_t>(size) + 8;
            
            if ((size & 1) && (total_chunk_size + sum_size + 1 <= full_size)) {
                total_chunk_size += 1;
            }

            if (!checked_range(data_offset, size, reader.size())) {
                return std::unexpected(std::string("WAV I/O failed"));
            }

            switch (sig) {
            case FMT_MAGIC: {
                if (size < 16) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                
                m_format.compression_mode = reader.read_le<uint16_t>();
                m_format.channels = reader.read_le<uint16_t>();
                m_format.sample_rate = reader.read_le<uint32_t>();
                m_format.avg_bytes_per_sec = reader.read_le<uint32_t>();
                m_format.block_align = reader.read_le<uint16_t>();
                m_format.bit_depth = reader.read_le<uint16_t>();
                
                if (m_format.compression_mode == WAVE_FORMAT_EXTENSIBLE) {
                    if (size < 40) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                    m_format.extension_size = reader.read_le<uint16_t>();
                    if (m_format.extension_size < 22) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                    m_format.valid_bits_per_sample = reader.read_le<uint16_t>();
                    m_format.channel_mask = reader.read_le<uint32_t>();
                    m_format.sub_format.Data1 = reader.read_le<uint32_t>();
                    m_format.sub_format.Data2 = reader.read_le<uint16_t>();
                    m_format.sub_format.Data3 = reader.read_le<uint16_t>();
                    m_format.sub_format.Data4 = reader.read_le<uint64_t>();

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
                if (size < 36) return std::unexpected(std::string("WAV parse failed: invalid smpl loop data"));
                
                m_sampler.manufacturer = reader.read_le<uint32_t>();
                m_sampler.product = reader.read_le<uint32_t>();
                m_sampler.sample_period = reader.read_le<uint32_t>();
                m_sampler.midi_unity_note = reader.read_le<uint32_t>();
                m_sampler.midi_pitch_fraction = reader.read_le<uint32_t>();
                m_sampler.smpte_format = reader.read_le<uint32_t>();
                m_sampler.smpte_offset = reader.read_le<uint32_t>();
                uint32_t num_loops = reader.read_le<uint32_t>();
                uint32_t sampler_data_size = reader.read_le<uint32_t>();
                
                const uint64_t expected_size = 36ull + static_cast<uint64_t>(num_loops) * 24ull + sampler_data_size;
                if (size < expected_size) return std::unexpected(std::string("WAV parse failed: invalid smpl loop data"));

                for (uint32_t i = 0; i < num_loops; ++i) {
                    SampleLoop loop;
                    loop.cue_point_id = reader.read_le<uint32_t>();
                    loop.type = reader.read_le<uint32_t>();
                    loop.start = reader.read_le<uint32_t>();
                    loop.end = reader.read_le<uint32_t>();
                    loop.fraction = reader.read_le<uint32_t>();
                    loop.play_count = reader.read_le<uint32_t>();
                    m_sampler.loops.push_back(loop);
                }
                if (sampler_data_size > 0) {
                    auto data_span = reader.read_bytes(sampler_data_size);
                    m_sampler.sampler_data.assign(data_span.begin(), data_span.end());
                }
                break;
            }
            case DATA_MAGIC: {
                m_pcm_offset = data_offset;
                m_pcm_size = size;
                break;
            }
            case CUE_MAGIC: {
                if (size < 4) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                uint32_t num_cues = reader.read_le<uint32_t>();
                const uint64_t expected_size = 4ull + static_cast<uint64_t>(num_cues) * 24ull;
                if (size < expected_size) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                
                for (uint32_t i = 0; i < num_cues; ++i) {
                    CuePoint cp;
                    cp.name = reader.read_le<uint32_t>();
                    cp.position = reader.read_le<uint32_t>();
                    cp.chunk_id = reader.read_le<uint32_t>();
                    cp.chunk_start = reader.read_le<uint32_t>();
                    cp.block_start = reader.read_le<uint32_t>();
                    cp.sample_offset = reader.read_le<uint32_t>();
                    m_cues.push_back(cp);
                }
                break;
            }
            default:
                break;
            }

            sum_size += total_chunk_size;
            
            if (sum_size > full_size) return std::unexpected(std::string("WAV parse failed: chunk table exceeds RIFF size"));
            
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
        const size_t storage_bytes = storage_bytes_for_bits(pcm.storage_bits);
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
        const auto pcm = bounded_subspan(m_source, m_pcm_offset, m_pcm_size);
        if (!checked_range(offset, decoder->sample_bytes, pcm.size())) {
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
        const auto pcm_bytes = bounded_subspan(m_source, m_pcm_offset, m_pcm_size);
        if (pcm_bytes.size() != m_pcm_size) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }

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
        auto layout = make_write_layout(pcm_data, sample_rate, channels, loops);
        if (!layout) {
            return std::unexpected(layout.error());
        }

        io::writer writer;
        auto open_res = writer.open(std::filesystem::path(path));
        if (!open_res) {
            return std::unexpected(std::string("WAV write failed: could not open output file"));
        }

        emit_wave_header(
            [&](uint16_t value) { writer.write_le<uint16_t>(value); },
            [&](uint32_t value) { writer.write_le<uint32_t>(value); },
            *layout,
            sample_rate,
            channels,
            loops
        );
        if (auto written = writer.write(pcm_data.data(), pcm_data.size_bytes()); !written) {
            return std::unexpected(std::string("WAV write failed"));
        }

        auto close_res = writer.close();
        if (!close_res) {
            return std::unexpected(std::string("WAV write failed"));
        }

        return {};
    }

}
