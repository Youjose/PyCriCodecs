/**
 * @file adx_decoder.cpp
 * @brief ADX/AHX header parsing and ADX ADPCM decode.
 *
 * The first C++ decoder behavior was ported from VGAudio/vgmstream-style ADX
 * references, then cross-checked against CRI adxencd. Official
 * evidence takes precedence where the older references disagree.
 *
 * Attribution:
 * - Initial decode references: VGAudio and vgmstream.
 * - Current behavior checks: CRI adxencd.
 * - CriCodecs C++23 port and reverse-engineering follow-up by Youjose.
 */

#include "adx_codec.hpp"

#include "../utilities/io_endian.hpp"
#include "../utilities/numeric.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace cricodecs::adx {

using cricodecs::util::divide_round_up;

    static constexpr uint16_t ADX_SIGNATURE = 0x8000;
    static constexpr uint16_t ADX_EOF_SCALE = 0x8001;
    static constexpr size_t ADX_FLAG_OFFSET = 19;
    static constexpr const char* CRI_STRING = "(c)CRI";
    
    static constexpr int16_t STATIC_COEFFICIENTS[8] = {
        0x0000, 0x0000,
        0x0F00, 0x0000,
        0x1CC0, static_cast<int16_t>(0xF300),
        0x1880, static_cast<int16_t>(0xF240)
    };

    [[nodiscard]] static constexpr int32_t sign_extend_4bit_sample(uint8_t sample) noexcept {
        return static_cast<int32_t>((sample & 0x0F) ^ 0x08) - 8;
    }

    [[nodiscard]] static bool has_cri_signature(std::span<const uint8_t> data, size_t offset) {
        return offset <= data.size() &&
            data.size() - offset >= 6 &&
            std::equal(CRI_STRING, CRI_STRING + 6, data.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    std::expected<Adx, AdxError> Adx::load(const std::filesystem::path& path) {
        Adx adx;
        auto load_result = adx.m_decoder.load(path.string());
        if (!load_result) {
            return std::unexpected(load_result.error());
        }

        adx.m_decoder.m_reader.close();
        adx.m_source_path = path;
        return adx;
    }

    std::expected<Adx, AdxError> Adx::load(std::span<const uint8_t> data) {
        Adx adx;
        adx.m_source_bytes.assign(data.begin(), data.end());
        auto load_result = adx.m_decoder.load(std::span<const uint8_t>(adx.m_source_bytes));
        if (!load_result) {
            return std::unexpected(load_result.error());
        }
        return adx;
    }

    void Adx::copy_decode_settings_to(AdxDecoder& decoder) const {
        decoder.m_key = m_decoder.m_key;
        decoder.m_ahx_key = m_decoder.m_ahx_key;
    }

    std::expected<AdxDecodeResult, AdxError> Adx::decode() {
        if (m_source_path.empty()) {
            return m_decoder.decode();
        }

        AdxDecoder decoder;
        auto load_result = decoder.load(m_source_path.string());
        if (!load_result) {
            return std::unexpected(load_result.error());
        }
        copy_decode_settings_to(decoder);
        return decoder.decode();
    }

    std::expected<void, AdxError> Adx::decode_into(std::span<int16_t> pcm_output) {
        if (m_source_path.empty()) {
            return m_decoder.decode_into(pcm_output);
        }

        AdxDecoder decoder;
        auto load_result = decoder.load(m_source_path.string());
        if (!load_result) {
            return std::unexpected(load_result.error());
        }
        copy_decode_settings_to(decoder);
        return decoder.decode_into(pcm_output);
    }

    std::expected<std::vector<uint8_t>, AdxError> Adx::source_bytes(
        std::string_view context) const {
        if (m_source_path.empty()) {
            return m_source_bytes;
        }
        return io::read_file_bytes(m_source_path, context);
    }

    std::expected<std::vector<uint8_t>, AdxError> Adx::rebuild() const {
        return source_bytes("ADX rebuild failed");
    }

    std::expected<std::vector<uint8_t>, AdxError> Adx::decrypt() const {
        auto bytes = source_bytes("ADX decrypt failed");
        if (!bytes) {
            return std::unexpected(bytes.error());
        }

        if (!is_encrypted()) {
            return std::move(*bytes);
        }

        if (is_ahx()) {
            if (!m_decoder.m_ahx_key) {
                return std::unexpected(AdxError("AHX decryption key required"));
            }
            auto decrypted = ahx::decrypt(*bytes, m_decoder.ahx_config());
            if (!decrypted) {
                return std::unexpected(decrypted.error());
            }
            if (decrypted->size() > ADX_FLAG_OFFSET) {
                (*decrypted)[ADX_FLAG_OFFSET] = 0;
            }
            return std::move(*decrypted);
        }

        if (!m_decoder.m_key) {
            return std::unexpected(AdxError("ADX decryption key required"));
        }

        const auto data_start = static_cast<size_t>(m_decoder.m_header.data_offset) + 4u;
        if (data_start > bytes->size()) {
            return std::unexpected(AdxError("ADX audio data offset is out of bounds"));
        }

        auto key_state = *m_decoder.m_key;
        const size_t frame_count = static_cast<size_t>(divide_round_up(
            m_decoder.m_header.sample_count, m_decoder.samples_per_block())) *
            m_decoder.m_header.channels;
        size_t cursor = data_start;
        for (size_t frame = 0; frame < frame_count; ++frame) {
            if (cursor + m_decoder.m_header.block_size > bytes->size()) {
                break;
            }
            const auto scale = io::read_be<uint16_t>(bytes->data() + cursor);
            if (scale == ADX_EOF_SCALE) break;

            const uint16_t mask = m_decoder.m_header.flags == 0x09 ? 0x1FFFu : 0x7FFFu;
            io::write_be<uint16_t>(bytes->data() + cursor,
                static_cast<uint16_t>((scale ^ key_state.xor_value) & mask));
            cursor += m_decoder.m_header.block_size;
            key_state.advance();
        }

        if (bytes->size() > ADX_FLAG_OFFSET) {
            (*bytes)[ADX_FLAG_OFFSET] = 0;
        }
        return std::move(*bytes);
    }

    std::expected<std::vector<uint8_t>, AdxError> Adx::encode(
        const AdxEncodeConfig& config,
        std::span<const AdxLoop> loops
    ) {
        auto decoded = decode();
        if (!decoded) {
            return std::unexpected(decoded.error());
        }

        auto effective_config = config;
        effective_config.sample_rate = decoded->sample_rate;
        effective_config.channels = decoded->channels;

        std::span<const AdxLoop> effective_loops = loops;
        if (effective_loops.empty() && decoded->has_loops) {
            effective_loops = std::span<const AdxLoop>(decoded->loops.data(), decoded->loops.size());
        }

        return AdxEncoder::encode(decoded->pcm_data, effective_config, effective_loops);
    }

    std::expected<void, AdxError> AdxDecoder::load(const std::string& path) {
        m_loaded = false;
        if (auto opened = m_reader.open(std::filesystem::path(path)); !opened) {
            return std::unexpected(AdxError("Failed to open ADX file"));
        }
        return parse_header();
    }

    std::expected<void, AdxError> AdxDecoder::load(std::span<const uint8_t> data) {
        m_loaded = false;
        if (auto opened = m_reader.open(data); !opened) {
            return std::unexpected(AdxError("Failed to open ADX data buffer"));
        }
        return parse_header();
    }

    std::expected<void, AdxError> AdxDecoder::parse_header() {
        m_loaded = false;
        m_coefficients[0] = 0;
        m_coefficients[1] = 0;
        m_header = {};
        m_loops.clear();
        m_history.clear();

        if (m_reader.size() < 20) return std::unexpected(AdxError("Invalid ADX header: file is too small"));

        m_reader.seek(0);

        m_header.signature = m_reader.read_be<uint16_t>();
        if (m_header.signature != ADX_SIGNATURE) {
            return std::unexpected(AdxError("Invalid ADX header: missing ADX signature"));
        }
        
        m_header.data_offset = m_reader.read_be<uint16_t>();
        m_header.encoding_mode = m_reader.read_le<uint8_t>();
        m_header.block_size = m_reader.read_le<uint8_t>();
        m_header.bit_depth = m_reader.read_le<uint8_t>();
        m_header.channels = m_reader.read_le<uint8_t>();
        m_header.sample_rate = m_reader.read_be<uint32_t>();
        m_header.sample_count = m_reader.read_be<uint32_t>();
        m_header.highpass_freq = m_reader.read_be<uint16_t>();
        m_header.version = m_reader.read_le<uint8_t>();
        m_header.flags = m_reader.read_le<uint8_t>();
        
        if (m_header.data_offset < 2) {
            return std::unexpected(AdxError("Invalid ADX header: data offset is too small"));
        }
        const size_t cri_offset = static_cast<size_t>(m_header.data_offset) - 2;

        if (is_ahx()) {
            if (m_header.block_size != 0 || m_header.bit_depth != 0 || m_header.version != 0x06) {
                return std::unexpected(AdxError("Invalid AHX header layout"));
            }
            if (m_header.channels == 0) {
                return std::unexpected(AdxError("Invalid ADX header: channel count is zero"));
            }

            if (!has_cri_signature(m_reader.data(), cri_offset)) {
                return std::unexpected(AdxError("Missing CRI string in AHX header"));
            }

            m_loaded = true;
            return {};
        }

        if (m_header.encoding_mode != 2 && m_header.encoding_mode != 3 && m_header.encoding_mode != 4) {
            return std::unexpected(AdxError("Invalid ADX encoding mode"));
        }
        
        if (m_header.version != 3 && m_header.version != 4 && m_header.version != 5) {
            return std::unexpected(AdxError("Invalid ADX version"));
        }
        
        if (m_header.block_size <= 2) {
            return std::unexpected(AdxError("Invalid ADX block size"));
        }

        if (m_header.bit_depth == 0 || m_header.bit_depth >= 16) {
            return std::unexpected(AdxError("Invalid ADX bit depth"));
        }
        
        if (m_header.channels == 0) {
            return std::unexpected(AdxError("Invalid ADX header: channel count is zero"));
        }
        
        const uint32_t payload_bits = static_cast<uint32_t>(m_header.block_size - 2u) * 8u;
        if (payload_bits % m_header.bit_depth != 0) {
            return std::unexpected(AdxError("Invalid ADX bit depth"));
        }
        
        size_t base_offset = 20;
        
        if (m_header.version == 4) {
            base_offset += 4;
            
            size_t history_count = m_header.channels > 1 ? m_header.channels : 2;
            m_history.resize(history_count);
            if (base_offset + history_count * 4 > m_reader.size()) {
                return std::unexpected(AdxError("ADX history block extends past the file"));
            }
            
            for (auto& history : std::span(m_history).first(m_header.channels)) {
                history = {
                    .prev1 = m_reader.read_be<int16_t>(),
                    .prev2 = m_reader.read_be<int16_t>(),
                };
            }
            if (m_header.channels == 1) {
                m_reader.skip(4);
            }
            base_offset += history_count * 4;
            
        }
        
        if (m_header.version != 5 && base_offset + 24 <= m_header.data_offset - 2u) {
            m_reader.seek(base_offset);
            m_reader.skip(2);
            const uint16_t loop_count = m_reader.read_be<uint16_t>();
            
            if (loop_count != 0) {
                if (base_offset + 4 + loop_count * 20 > m_header.data_offset - 2u) {
                    return std::unexpected(AdxError("Invalid ADX loop metadata"));
                }
                
                m_loops.resize(loop_count);
                for (uint16_t i = 0; i < loop_count; ++i) {
                    m_loops[i].index = m_reader.read_be<uint16_t>();
                    m_loops[i].type = m_reader.read_be<uint16_t>();
                    m_loops[i].start_sample = m_reader.read_be<uint32_t>();
                    m_loops[i].start_byte = m_reader.read_be<uint32_t>();
                    m_loops[i].end_sample = m_reader.read_be<uint32_t>();
                    m_loops[i].end_byte = m_reader.read_be<uint32_t>();
                }
            }
        }
        
        if (!has_cri_signature(m_reader.data(), cri_offset)) {
            return std::unexpected(AdxError("Missing CRI string in ADX header"));
        }
        
        calculate_coefficients();
        
        m_loaded = true;
        return {};
    }

    uint32_t AdxDecoder::samples_per_block() const noexcept {
        return static_cast<uint32_t>(m_header.block_size - 2u) * 8u / m_header.bit_depth;
    }

    ahx::AhxDecodeConfig AdxDecoder::ahx_config() const {
        return {
            .encoding_mode = m_header.encoding_mode,
            .sample_rate = m_header.sample_rate,
            .sample_count = m_header.sample_count,
            .channels = m_header.channels,
            .encryption_type = m_header.flags,
            .start_offset = static_cast<size_t>(m_header.data_offset) + 4u,
            .key = m_ahx_key.value_or(ahx::AhxKey{}),
        };
    }

    void AdxDecoder::calculate_coefficients() {
        if (m_header.encoding_mode == 2) {
            m_coefficients[0] = 0;
            m_coefficients[1] = 0;
        } else {
            const double a = std::numbers::sqrt2 - std::cos(
                2.0 * std::numbers::pi * m_header.highpass_freq / m_header.sample_rate);
            constexpr double b = std::numbers::sqrt2 - 1.0;
            const double c = (a - std::sqrt((a + b) * (a - b))) / b;
            
            m_coefficients[0] = static_cast<int32_t>(c * 8192.0);
            m_coefficients[1] = static_cast<int32_t>(c * c * -4096.0);
        }
    }
    
    void AdxDecoder::set_shared_key(AdxKeyState key) {
        m_key = key;
        m_ahx_key = ahx::AhxKey{
            .start = key.xor_value,
            .mult = key.mult,
            .add = key.add,
        };
    }

    void AdxDecoder::set_key_type8(std::string_view key) {
        set_shared_key(key8_derive(key));
    }
    
    void AdxDecoder::set_key_type9(uint64_t key, uint16_t subkey) {
        set_shared_key(key9_derive(key, subkey));
    }

    void AdxDecoder::set_key_triplet(uint16_t start, uint16_t mult, uint16_t add) {
        m_key = AdxKeyState{start, mult, add};
    }

    void AdxDecoder::set_ahx_key(uint16_t start, uint16_t mult, uint16_t add) {
        m_ahx_key = ahx::AhxKey{start, mult, add};
    }

    void AdxDecoder::decode_block(io::reader& reader, int16_t* output,
                                   size_t output_stride, AdpcmHistory& history,
                                   AdxKeyState* key_state,
                                   uint32_t samples_to_decode) {
        uint16_t scale_raw = reader.read_be<uint16_t>();
        
        int32_t coef0 = m_coefficients[0];
        int32_t coef1 = m_coefficients[1];
        int32_t scale;
        
        if (key_state) {
            scale_raw = (scale_raw ^ key_state->xor_value) & 0x7FFF;
        }
        
        switch (m_header.encoding_mode) {
            case 4:
                scale = 1 << (12 - scale_raw);
                break;
            case 2: {
                int predictor = scale_raw >> 13;
                scale = (scale_raw & 0x1FFF) + 1;
                coef0 = STATIC_COEFFICIENTS[predictor * 2 + 0];
                coef1 = STATIC_COEFFICIENTS[predictor * 2 + 1];
                break;
            }
            default:
                scale = (scale_raw & 0x1FFF) + 1;
                break;
        }
        
        const auto sample_bytes = reader.read_bytes(m_header.block_size - 2u);
        int16_t hist1 = history.prev1;
        int16_t hist2 = history.prev2;

        const auto decode_sample = [&](int32_t sample) {
            int32_t predicted;
            if (m_header.version == 3) {
                predicted = sample * scale +
                           (coef0 * static_cast<int32_t>(hist1) >> 12) +
                           (coef1 * static_cast<int32_t>(hist2) >> 12);
            } else {
                predicted = sample * scale +
                           ((coef0 * static_cast<int32_t>(hist1) +
                             coef1 * static_cast<int32_t>(hist2)) >> 12);
            }

            int16_t decoded = static_cast<int16_t>(util::clamp(predicted, -32768, 32767));

            *output = decoded;
            output += output_stride;

            hist2 = hist1;
            hist1 = decoded;
        };

        if (m_header.bit_depth == 4) {
            if (samples_to_decode == samples_per_block()) {
                for (const uint8_t byte : sample_bytes) {
                    decode_sample(sign_extend_4bit_sample(byte >> 4));
                    decode_sample(sign_extend_4bit_sample(byte));
                }
                history.prev1 = hist1;
                history.prev2 = hist2;
                return;
            }

            uint32_t decoded_count = 0;
            for (const uint8_t byte : sample_bytes) {
                decode_sample(sign_extend_4bit_sample(byte >> 4));
                if (++decoded_count >= samples_to_decode) {
                    break;
                }

                decode_sample(sign_extend_4bit_sample(byte));
                if (++decoded_count >= samples_to_decode) {
                    break;
                }
            }
            history.prev1 = hist1;
            history.prev2 = hist2;
            return;
        }

        io::bit_reader bits(sample_bytes);
        const uint32_t sign_bit = 1u << (m_header.bit_depth - 1u);
        const uint32_t sign_value = 1u << m_header.bit_depth;
        for (uint32_t i = 0; i < samples_to_decode; ++i) {
            const uint32_t encoded = bits.read(m_header.bit_depth);
            const int32_t sample = encoded & sign_bit
                ? static_cast<int32_t>(encoded) - static_cast<int32_t>(sign_value)
                : static_cast<int32_t>(encoded);
            decode_sample(sample);
        }

        history.prev1 = hist1;
        history.prev2 = hist2;
    }

    std::expected<void, AdxError> AdxDecoder::decode_into(std::span<int16_t> pcm_output) {
        if (!m_loaded) {
            return std::unexpected(AdxError("ADX data has not been loaded"));
        }

        if (is_ahx()) {
            return std::unexpected(AdxError("AHX decode into caller-owned PCM is not implemented"));
        }

        const bool encrypted = (m_header.flags == 0x08 || m_header.flags == 0x09);
        if (encrypted && !m_key) {
            return std::unexpected(AdxError("ADX decryption key required"));
        }

        const size_t valid_samples = static_cast<size_t>(m_header.sample_count) * m_header.channels;
        if (pcm_output.size() < valid_samples) {
            return std::unexpected(AdxError("ADX decode output buffer is too small"));
        }

        auto channel_history = m_history;
        channel_history.resize(m_header.channels);
        AdxKeyState current_key_state = m_key.value_or(AdxKeyState{});

        size_t data_start = m_header.data_offset + 4;
        if (data_start > m_reader.size()) {
            return std::unexpected(AdxError("ADX audio data offset is out of bounds"));
        }

        io::reader reader;
        if (auto result = reader.open(m_reader.data().subspan(data_start)); !result) {
            return std::unexpected(AdxError("Failed to bind ADX decode buffer"));
        }

        const uint32_t block_samples = samples_per_block();
        const uint32_t blocks_per_channel = divide_round_up(m_header.sample_count, block_samples);
        const size_t frame_count = static_cast<size_t>(blocks_per_channel) * m_header.channels;
        size_t frame = 0;
        bool truncated = false;
        for (uint32_t block = 0; block < blocks_per_channel && !truncated; ++block) {
            const uint32_t block_sample_start = block * block_samples;
            const uint32_t remaining_samples = m_header.sample_count - block_sample_start;
            const uint32_t samples_this_block = std::min(block_samples, remaining_samples);
            for (uint8_t channel = 0; channel < m_header.channels; ++channel) {
                if (reader.remaining() < 2) {
                    truncated = true;
                    break;
                }
                const size_t frame_start = reader.tell();
                if (reader.read_be<uint16_t>() == ADX_EOF_SCALE ||
                    reader.remaining() + 2u < m_header.block_size) {
                    truncated = true;
                    break;
                }
                reader.seek(frame_start);
                int16_t* output = pcm_output.data() +
                    static_cast<size_t>(block_sample_start) * m_header.channels + channel;
                decode_block(reader, output, m_header.channels, channel_history[channel],
                             encrypted ? &current_key_state : nullptr, samples_this_block);
                if (encrypted) current_key_state.advance();
                ++frame;
            }
        }

        if (frame < frame_count) {
            const uint32_t block = static_cast<uint32_t>(frame / m_header.channels);
            const uint8_t first_channel = static_cast<uint8_t>(frame % m_header.channels);
            const uint32_t block_start = block * block_samples;
            const uint32_t block_end = std::min(block_start + block_samples, m_header.sample_count);
            for (uint32_t sample = block_start; sample < block_end; ++sample) {
                auto* output = pcm_output.data() + static_cast<size_t>(sample) * m_header.channels;
                std::fill(output + first_channel, output + m_header.channels, int16_t{0});
            }
            std::fill(pcm_output.begin() + static_cast<size_t>(block_end) * m_header.channels,
                      pcm_output.begin() + static_cast<std::ptrdiff_t>(valid_samples), int16_t{0});
        }

        return {};
    }

    std::expected<AdxDecodeResult, AdxError> AdxDecoder::decode() {
        if (!m_loaded) {
            return std::unexpected(AdxError("ADX data has not been loaded"));
        }

        if (is_ahx()) {
            if (is_encrypted() && !m_ahx_key) {
                return std::unexpected(AdxError("AHX decryption key required"));
            }
            auto pcm = ahx::decode(m_reader.data(), ahx_config());
            if (!pcm) {
                return std::unexpected(pcm.error());
            }

            return AdxDecodeResult{
                .pcm_data = std::move(*pcm),
                .sample_rate = m_header.sample_rate,
                .channels = m_header.channels,
                .sample_count = m_header.sample_count,
            };
        }
        
        bool encrypted = (m_header.flags == 0x08 || m_header.flags == 0x09);
        if (encrypted && !m_key) {
            return std::unexpected(AdxError("ADX decryption key required"));
        }
        
        AdxDecodeResult result;
        result.sample_rate = m_header.sample_rate;
        result.channels = m_header.channels;
        result.sample_count = m_header.sample_count;
        result.loops = m_loops;
        result.has_loops = !result.loops.empty();
        if (result.has_loops) {
            result.loop_start = result.loops.front().start_sample;
            result.loop_end = result.loops.front().end_sample;
        }
        
        size_t valid_samples = static_cast<size_t>(m_header.sample_count) * m_header.channels;
        result.pcm_data.resize(valid_samples);

        if (auto decoded = decode_into(result.pcm_data); !decoded) {
            return std::unexpected(decoded.error());
        }

        return result;
    }

} // namespace cricodecs::adx
