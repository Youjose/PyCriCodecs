/**
 * @file adx_encoder.cpp
 * @brief ADX/AHX encode dispatch and ADX ADPCM frame writer.
 *
 * The ADX encoder began from VGAudio behavior, then was narrowed against CRI
 * adxencd for loop layout, header offsets, version behavior, and encryption
 * boundaries.
 *
 * Attribution:
 * - Initial encode reference: VGAudio.
 * - Current behavior checks: CRI adxencd.
 * - CriCodecs C++23 port and reverse-engineering follow-up by Youjose.
 */

#include "adx_codec.hpp"

#include "../utilities/io_endian.hpp"
#include "../utilities/numeric.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace cricodecs::adx {

using cricodecs::util::divide_round_up;
using io::append_be;

    static constexpr uint16_t ADX_EOF_SCALE = 0x8001;
    static constexpr uint8_t ADX_FRAME_BYTES = 18;
    static constexpr uint8_t ADX_SCALE_BYTES = 2;
    static constexpr uint8_t ADX_NIBBLE_BYTES = ADX_FRAME_BYTES - ADX_SCALE_BYTES;
    static constexpr uint8_t ADX_BIT_DEPTH = 4;
    static constexpr uint8_t ADX_SAMPLES_PER_BLOCK = ADX_NIBBLE_BYTES * 2;
    static constexpr uint32_t ADX_LOOP_BOUNDARY = 0x800;
    static constexpr std::string_view CRI_SIGNATURE = "(c)CRI";

    struct AdxLoopLayout final {
        uint32_t alignment_samples = 0;
        uint32_t stored_data_offset = 0;
        uint32_t audio_offset = 0;
    };

    [[nodiscard]] static std::expected<std::vector<AdxLoop>, AdxError> normalize_official_loops(
        std::span<const AdxLoop> loops)
    {
        std::vector<AdxLoop> normalized;
        normalized.reserve(loops.size());
        for (const auto& loop : loops) {
            if (loop.start_sample > loop.end_sample) {
                return std::unexpected(AdxError("ADX loop start sample must not exceed end sample"));
            }
            if (loop.start_sample == loop.end_sample) {
                continue;
            }
            normalized.push_back(loop);
        }
        return normalized;
    }

    [[nodiscard]] static std::expected<AdxLoopLayout, AdxError> calculate_loop_layout(
        uint32_t header_struct_size,
        uint8_t channels,
        uint32_t original_sample_count,
        std::span<const AdxLoop> loops)
    {
        const auto& first_loop = loops.front();
        if (first_loop.end_sample > original_sample_count) {
            return std::unexpected(AdxError("ADX loop end sample exceeds source PCM length"));
        }

        const uint32_t alignment_unit = channels == 1
            ? ADX_SAMPLES_PER_BLOCK * 2
            : ADX_SAMPLES_PER_BLOCK;
        const uint32_t alignment_samples =
            (alignment_unit - first_loop.start_sample % alignment_unit) % alignment_unit;
        const uint32_t pre_loop_frames =
            (alignment_samples + first_loop.start_sample) / ADX_SAMPLES_PER_BLOCK;
        const uint32_t pre_loop_bytes = pre_loop_frames * ADX_FRAME_BYTES * channels;
        // `adx_porting_notes.md` captures this writer formula as
        // align_up(header_base + pre_loop_bytes + 4, 0x800).
        const uint32_t loop_start_target = cricodecs::util::align_up(
            header_struct_size + pre_loop_bytes + 4, ADX_LOOP_BOUNDARY);

        return AdxLoopLayout{
            .alignment_samples = alignment_samples,
            .stored_data_offset = loop_start_target - pre_loop_bytes - 4,
            .audio_offset = loop_start_target - pre_loop_bytes,
        };
    }

    [[nodiscard]] static std::expected<std::vector<uint8_t>, AdxError> encode_ahx(
        std::span<const int16_t> pcm_data, const AdxEncodeConfig& config) {
        ahx::AhxKey key = config.ahx_key;
        if (key.empty()) {
            const AdxKeyState derived = config.encryption_type == 0x09
                ? key9_derive(config.key64, config.subkey)
                : key8_derive(config.key_string);
            key = {.start = derived.xor_value, .mult = derived.mult, .add = derived.add};
        }
        return ahx::encode(pcm_data, ahx::AhxEncodeConfig{
            .encoding_mode = config.encoding_mode,
            .sample_rate = config.sample_rate,
            .channels = config.channels,
            .encryption_type = config.encryption_type,
            .key = key,
            .bit_allocation_pattern = config.ahx_bit_allocation_pattern,
        });
    }

    static void write_header(
        std::vector<uint8_t>& buffer,
        const AdxEncodeConfig& config,
        uint32_t samples_per_channel,
        uint32_t stored_data_offset,
        uint32_t audio_offset,
        std::span<const AdxLoop> loops,
        const AdxLoopLayout& layout
    ) {
        append_be<uint16_t>(buffer, 0x8000u);
        append_be<uint16_t>(buffer, static_cast<uint16_t>(stored_data_offset));
        buffer.insert(buffer.end(), {
            config.encoding_mode, config.block_size, config.bit_depth, config.channels});
        append_be<uint32_t>(buffer, config.sample_rate);
        append_be<uint32_t>(buffer, samples_per_channel);
        append_be<uint16_t>(buffer, config.highpass_freq);
        buffer.push_back(config.version);
        buffer.push_back(config.encryption_type);

        if (config.version == 4) {
            const size_t history_count = config.channels > 1 ? config.channels : 2;
            buffer.resize(buffer.size() + 4u + history_count * 4u, 0);
        }

        if (!loops.empty()) {
            // TODO(adx): Later-tool `-nodelterm` defaults still need direct
            // official coverage before the loop surface can be widened.
            append_be<uint16_t>(buffer, static_cast<uint16_t>(layout.alignment_samples));
            append_be<uint16_t>(buffer, static_cast<uint16_t>(loops.size()));
            for (const auto& loop : loops) {
                const uint32_t start_sample = loop.start_sample + layout.alignment_samples;
                const uint32_t end_sample = loop.end_sample + layout.alignment_samples;
                const uint32_t frame_bytes = ADX_FRAME_BYTES * config.channels;
                append_be<uint16_t>(buffer, loop.index);
                append_be<uint16_t>(buffer, loop.type == 0 ? 1 : loop.type);
                append_be<uint32_t>(buffer, start_sample);
                append_be<uint32_t>(buffer, audio_offset +
                    divide_round_up(start_sample, ADX_SAMPLES_PER_BLOCK) * frame_bytes);
                append_be<uint32_t>(buffer, end_sample);
                append_be<uint32_t>(buffer, audio_offset +
                    divide_round_up(end_sample, ADX_SAMPLES_PER_BLOCK) * frame_bytes);
            }
        }

        buffer.resize(std::max(buffer.size(), audio_offset - CRI_SIGNATURE.size()), 0);
        buffer.insert(buffer.end(), CRI_SIGNATURE.begin(), CRI_SIGNATURE.end());
        buffer.resize(std::max(buffer.size(), static_cast<size_t>(audio_offset)), 0);
    }

    void AdxEncoder::calculate_coefficients(int32_t* coeffs, uint16_t highpass_freq, uint32_t sample_rate) {
        if (highpass_freq == 0 || sample_rate == 0) {
            coeffs[0] = 0;
            coeffs[1] = 0;
            return;
        }
        
        const double a = std::numbers::sqrt2 - std::cos(
            2.0 * std::numbers::pi * highpass_freq / sample_rate);
        constexpr double b = std::numbers::sqrt2 - 1.0;
        const double c = (a - std::sqrt((a + b) * (a - b))) / b;
        
        coeffs[0] = static_cast<int32_t>(c * 8192.0);
        coeffs[1] = static_cast<int32_t>(c * c * -4096.0);
    }

    void AdxEncoder::encode_block(
        std::vector<uint8_t>& buffer,
        std::span<const int16_t> samples,
        int32_t* coeffs,
        AdpcmHistory& history,
        const AdxEncodeConfig& config,
        AdxKeyState* key_state
    ) {
        constexpr uint32_t samples_per_block = ADX_SAMPLES_PER_BLOCK;
        constexpr int32_t limit = (1 << (ADX_BIT_DEPTH - 1)) - 1;

        const size_t channel_stride = config.channels;
        const uint32_t available_samples = samples.empty()
            ? 0
            : static_cast<uint32_t>((samples.size() + channel_stride - 1) / channel_stride);
        const bool full_block = available_samples >= samples_per_block;
        const int16_t* sample_data = samples.data();

        const auto read_full_sample = [&](uint32_t index) -> int16_t {
            return sample_data[static_cast<size_t>(index) * channel_stride];
        };
        const auto read_padded_sample = [&](uint32_t index) -> int16_t {
            return index < available_samples ? read_full_sample(index) : 0;
        };
        const auto predict = [&](int16_t hist1, int16_t hist2) {
            return config.version == 4
                ? (coeffs[0] * static_cast<int32_t>(hist1) +
                   coeffs[1] * static_cast<int32_t>(hist2)) >> 12
                : (coeffs[0] * static_cast<int32_t>(hist1) >> 12) +
                  (coeffs[1] * static_cast<int32_t>(hist2) >> 12);
        };

        int32_t minimum = 0, maximum = 0;
        const auto find_residual_bounds = [&](auto read_sample) {
            int16_t hist1 = history.prev1;
            int16_t hist2 = history.prev2;
            for (uint32_t i = 0; i < samples_per_block; ++i) {
                const int16_t current_sample = read_sample(i);
                int32_t sample = (((int32_t)current_sample << 12) - coeffs[0] * hist1 - coeffs[1] * hist2) >> 12;
                if (sample < minimum) minimum = sample;
                else if (sample > maximum) maximum = sample;
                hist2 = hist1;
                hist1 = current_sample;
            }
        };
        if (full_block) {
            find_residual_bounds(read_full_sample);
        } else {
            find_residual_bounds(read_padded_sample);
        }

        if (minimum == 0 && maximum == 0) {
            const size_t frame_offset = buffer.size();
            buffer.resize(frame_offset + ADX_FRAME_BYTES, 0);
            for (uint32_t i = 0; i < samples_per_block; ++i) {
                const auto decoded = static_cast<int16_t>(
                    util::clamp(predict(history.prev1, history.prev2), -32768, 32767));
                history.prev2 = history.prev1;
                history.prev1 = decoded;
            }
            return;
        }

        uint16_t scale = static_cast<uint16_t>(
            std::min(0x1000, std::max(maximum / limit, minimum / ~limit)));

        uint16_t scale_written;
        switch (config.encoding_mode) {
            case 4: {
                uint32_t power = std::bit_width(scale);
                scale = 1 << power;
                scale_written = static_cast<uint16_t>(12 - power);
                break;
            }
            case 2:
                scale_written = (config.filter_id << 13) | (scale & 0x1FFF);
                break;
            default:
                scale_written = scale;
                break;
        }

        // Type-9 reserves bit 0x1000 for key validation. CRI writes the
        // encoder's maximum scale (0x1000) as 0x0fff before masking it.
        if (config.encryption_type == 9 && config.encoding_mode == 3 &&
            scale_written == 0x1000u) {
            scale_written = 0x0FFFu;
        }

        uint16_t output_scale = scale_written;
        if (key_state) {
            const uint16_t mask = config.encryption_type == 9 ? 0x1FFFu : 0x7FFFu;
            output_scale = static_cast<uint16_t>((output_scale ^ key_state->xor_value) & mask);
        }

        const int32_t decode_scale = config.encoding_mode == 4
            ? 1 << (12 - scale_written)
            : (scale_written & 0x1FFF) + 1;

        int16_t hist1 = history.prev1;
        int16_t hist2 = history.prev2;
        int16_t enc_scale = (scale == 0) ? 1 : scale;

        const auto emit_encoded_samples = [&](auto read_sample) {
            const size_t frame_offset = buffer.size();
            buffer.resize(frame_offset + ADX_FRAME_BYTES);
            uint8_t* frame = buffer.data() + frame_offset;
            io::write_be<uint16_t>(frame, output_scale);
            uint8_t* payload = frame + ADX_SCALE_BYTES;

            const auto encode_sample = [&](uint32_t index) -> int32_t {
                int32_t delta = (((int32_t)read_sample(index) << 12) - coeffs[0] * hist1 - coeffs[1] * hist2) >> 12;

                delta = delta > 0 ? delta + (enc_scale >> 1) : delta - (enc_scale >> 1);
                delta /= enc_scale;
                delta = util::clamp(delta, -8, 7);

                const int32_t simulated = delta * decode_scale + predict(hist1, hist2);
                const auto decoded = static_cast<int16_t>(
                    util::clamp(simulated, -32768, 32767));

                hist2 = hist1;
                hist1 = decoded;

                return delta;
            };

            for (uint32_t i = 0; i < ADX_NIBBLE_BYTES; ++i) {
                const int32_t high = encode_sample(i * 2);
                const int32_t low = encode_sample(i * 2 + 1);
                payload[i] = static_cast<uint8_t>(((high & 0x0F) << 4) | (low & 0x0F));
            }
        };
        if (full_block) {
            emit_encoded_samples(read_full_sample);
        } else {
            emit_encoded_samples(read_padded_sample);
        }

        history.prev1 = hist1;
        history.prev2 = hist2;
    }

    std::expected<std::vector<uint8_t>, AdxError> AdxEncoder::encode(
        std::span<const int16_t> pcm_data,
        const AdxEncodeConfig& config,
        std::span<const AdxLoop> loops
    ) {
        if (config.encoding_mode == 0x10 || config.encoding_mode == 0x11) {
            if (!loops.empty()) {
                return std::unexpected(AdxError("AHX encoding does not support loop metadata"));
            }
            return encode_ahx(pcm_data, config);
        }

        if (config.channels == 0 || config.sample_rate == 0) {
            return std::unexpected(AdxError("Invalid ADX encode configuration: sample rate and channels are required"));
        }
        if (config.encoding_mode < 2 || config.encoding_mode > 4) {
            return std::unexpected(AdxError("Unsupported ADX encoding mode"));
        }
        if (config.block_size != ADX_FRAME_BYTES) {
            return std::unexpected(AdxError("Unsupported ADX encode configuration: ADX frames must be 18 bytes"));
        }
        if (config.bit_depth != ADX_BIT_DEPTH) {
            return std::unexpected(AdxError("Unsupported ADX encode configuration: ADX samples must be 4-bit nibbles"));
        }
        if (config.encryption_type != 0 && config.encryption_type != 8 && config.encryption_type != 9) {
            return std::unexpected(AdxError("Unsupported ADX encryption"));
        }

        const auto normalized_loops_result = normalize_official_loops(loops);
        if (!normalized_loops_result) {
            return std::unexpected(normalized_loops_result.error());
        }
        const auto& normalized_loops = *normalized_loops_result;

        std::vector<uint8_t> buffer;
        
        const uint32_t source_samples_per_channel = static_cast<uint32_t>(pcm_data.size()) / config.channels;
        
        uint32_t header_struct_size = 20;

        if (config.version == 4) {
            size_t hist_count = (config.channels > 1) ? config.channels : 2;
            header_struct_size += 4 + static_cast<uint32_t>(hist_count * 4);
        }

        const bool has_loops = !normalized_loops.empty();
        if (has_loops) {
            header_struct_size += 4 + static_cast<uint32_t>(normalized_loops.size() * 20);
        }

        const uint32_t frame_bytes = config.block_size * config.channels;
        const auto loop_layout_result = has_loops
            ? calculate_loop_layout(
                  header_struct_size,
                  config.channels,
                  source_samples_per_channel,
                  normalized_loops)
            : std::expected<AdxLoopLayout, AdxError>(AdxLoopLayout{});
        if (!loop_layout_result.has_value()) {
            return std::unexpected(loop_layout_result.error());
        }
        const AdxLoopLayout loop_layout = loop_layout_result.value();

        const uint32_t truncated_source_samples_per_channel =
            has_loops && config.delete_samples_after_loop_end
                ? normalized_loops.front().end_sample
                : source_samples_per_channel;

        std::vector<int16_t> padded_pcm;
        std::span<const int16_t> encoded_pcm = pcm_data;
        uint32_t samples_per_channel = truncated_source_samples_per_channel;
        if (has_loops && loop_layout.alignment_samples != 0) {
            samples_per_channel += loop_layout.alignment_samples;
            padded_pcm.assign(static_cast<size_t>(samples_per_channel) * config.channels, 0);
            const auto source = pcm_data.first(
                static_cast<size_t>(truncated_source_samples_per_channel) * config.channels);
            std::ranges::copy(source, padded_pcm.begin() +
                static_cast<size_t>(loop_layout.alignment_samples) * config.channels);
            encoded_pcm = padded_pcm;
        } else if (truncated_source_samples_per_channel != source_samples_per_channel) {
            encoded_pcm = pcm_data.first(static_cast<size_t>(truncated_source_samples_per_channel) * config.channels);
        }

        const uint32_t blocks_per_channel = divide_round_up(
            samples_per_channel, ADX_SAMPLES_PER_BLOCK);

        const uint32_t audio_offset = has_loops
            ? loop_layout.audio_offset
            : cricodecs::util::align_up(header_struct_size + 6, 4);
        const uint32_t stored_data_offset = has_loops
            ? loop_layout.stored_data_offset
            : audio_offset - 4;

        const size_t encoded_audio_bytes = static_cast<size_t>(blocks_per_channel) * frame_bytes;
        const size_t maximum_end_code_size = has_loops
            ? static_cast<size_t>(ADX_LOOP_BOUNDARY) + config.block_size - 1
            : config.block_size;
        buffer.reserve(static_cast<size_t>(audio_offset) + encoded_audio_bytes + maximum_end_code_size);

        write_header(buffer, config, samples_per_channel, stored_data_offset,
                     audio_offset, normalized_loops, loop_layout);

        const bool encrypted = config.encryption_type == 8 || config.encryption_type == 9;
        AdxKeyState current_key_state;
        if (encrypted) {
            if (config.encryption_type == 9) {
                current_key_state = key9_derive(config.key64, config.subkey);
            } else {
                current_key_state = key8_derive(config.key_string);
            }
        }

        int32_t coeffs[2];
        calculate_coefficients(coeffs, config.highpass_freq, config.sample_rate);
        
        std::vector<AdpcmHistory> histories(config.channels);
        
        if (config.version == 4 && samples_per_channel > 0) {
            for (uint32_t ch = 0; ch < config.channels; ++ch) {
                int16_t first = (ch < encoded_pcm.size()) ? encoded_pcm[ch] : 0;
                histories[ch].prev1 = first;
                histories[ch].prev2 = first;
            }
        }
        
        for (uint32_t block = 0; block < blocks_per_channel; ++block) {
            const size_t block_offset =
                static_cast<size_t>(block) * ADX_SAMPLES_PER_BLOCK * config.channels;
            for (uint8_t channel = 0; channel < config.channels; ++channel) {
                const size_t offset = block_offset + channel;
                const auto samples = offset < encoded_pcm.size() ? encoded_pcm.subspan(offset)
                                                                  : std::span<const int16_t>{};
                encode_block(buffer, samples, coeffs, histories[channel], config,
                             encrypted ? &current_key_state : nullptr);
                if (encrypted) current_key_state.advance();
            }
        }
        
        const size_t end_code_size = has_loops
            ? cricodecs::util::align_up(
                  buffer.size() + config.block_size,
                  static_cast<size_t>(ADX_LOOP_BOUNDARY)) - buffer.size()
            : config.block_size;

        append_be<uint16_t>(buffer, ADX_EOF_SCALE);
        append_be<uint16_t>(buffer, static_cast<uint16_t>(end_code_size - 4));
        buffer.resize(buffer.size() + end_code_size - 4, 0);
        
        return buffer;
    }

    std::expected<std::vector<uint8_t>, AdxError> AdxEncoder::encode(
        const wav::WavContainer& wav,
        const AdxEncodeConfig& config,
        std::span<const AdxLoop> loops
    ) {
        auto pcm = wav.get_pcm16();
        if (!pcm) {
            return std::unexpected(AdxError("ADX encode failed: ") + pcm.error());
        }

        auto effective_config = config;
        effective_config.sample_rate = wav.sample_rate();
        effective_config.channels = static_cast<uint8_t>(wav.channels());

        std::vector<AdxLoop> wav_loops;
        std::span<const AdxLoop> effective_loops = loops;
        if (effective_loops.empty()) {
            const auto& source_loops = wav.sampler().loops;
            wav_loops.reserve(source_loops.size());
            for (const auto& loop : source_loops) {
                wav_loops.push_back(AdxLoop{
                    .index = static_cast<uint16_t>(loop.cue_point_id),
                    .type = static_cast<uint16_t>(loop.type),
                    .start_sample = loop.start,
                    .start_byte = 0,
                    .end_sample = loop.end,
                    .end_byte = 0,
                });
            }
            effective_loops = wav_loops;
        }

        return encode(*pcm, effective_config, effective_loops);
    }

    std::expected<void, AdxError> AdxEncoder::encode_to_file(
        const std::string& path,
        std::span<const int16_t> pcm_data,
        const AdxEncodeConfig& config,
        std::span<const AdxLoop> loops
    ) {
        auto result = encode(pcm_data, config, loops);
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        
        io::writer writer;
        if (auto open_result = writer.open(std::filesystem::path(path)); !open_result) {
            return std::unexpected(AdxError("Failed to open ADX output file for writing: ") + open_result.error());
        }
        
        if (auto write_result = writer.write(std::span<const uint8_t>(result.value())); !write_result) {
            return std::unexpected(AdxError("Failed to write ADX output file: ") + write_result.error());
        }
        
        if (auto close_result = writer.close(); !close_result) {
            return std::unexpected(AdxError("Failed to finalize ADX output file: ") + close_result.error());
        }
        
        return {};
    }

} // namespace cricodecs::adx
