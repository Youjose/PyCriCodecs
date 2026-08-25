/**
 * @file usm_adx_key_recovery.cpp
 * @brief Recovery of the repeating USM mask applied to ADX audio chunks.
 */

#include "usm_key_recovery_internal.hpp"

#include "usm_container.hpp"
#include "usm_crypto.hpp"

#include "../adx/adx_codec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace cricodecs::usm {

namespace {

constexpr size_t mask_size = 0x20;
constexpr size_t plain_prefix_size = 0x140;
constexpr size_t zero_window_offset = plain_prefix_size - mask_size;
constexpr std::array<uint8_t, 4> fixed_audio_mask = {'U', 'R', 'U', 'C'};

using ByteHistogram = std::array<uint32_t, 256>;
using AudioMask = std::array<uint8_t, mask_size>;

struct AdxLayout {
    size_t data_offset = 0;
    uint8_t block_size = 0;
};

struct TrackEvidence {
    uint8_t block_size = 0;
    std::vector<ByteHistogram> plain_by_frame_byte;
    std::vector<ByteHistogram> cipher_by_mask_and_frame_byte;
    std::array<size_t, mask_size> column_observations{};
    size_t audio_chunks = 0;
    std::vector<AudioMask> zero_window_masks;
};

struct ScoredMask {
    AudioMask mask{};
    double confidence = 0.0;
};

[[nodiscard]] std::optional<AdxLayout> inspect_adx(std::span<const uint8_t> bytes) {
    adx::AdxDecoder decoder;
    if (!decoder.load(bytes) || decoder.is_ahx()) {
        return std::nullopt;
    }
    const auto& header = decoder.header();
    const size_t data_offset = static_cast<size_t>(header.data_offset) + 4u;
    if (data_offset > bytes.size()) {
        return std::nullopt;
    }
    return AdxLayout{.data_offset = data_offset, .block_size = header.block_size};
}

[[nodiscard]] constexpr size_t cipher_histogram_index(
    size_t column,
    size_t frame_byte,
    size_t block_size
) noexcept {
    return column * block_size + frame_byte;
}

[[nodiscard]] std::optional<TrackEvidence> collect_track(
    const UsmReader& source,
    UsmStreamId stream_id
) {
    const UsmChunk* first = nullptr;
    for (const auto& chunk : source.chunks()) {
        if (chunk.is_stream() && chunk.belongs_to(stream_id)) {
            first = &chunk;
            break;
        }
    }
    if (first == nullptr) {
        return std::nullopt;
    }
    const auto layout = inspect_adx(first->payload);
    if (!layout) {
        return std::nullopt;
    }

    TrackEvidence evidence;
    evidence.block_size = layout->block_size;
    evidence.plain_by_frame_byte.resize(layout->block_size);
    evidence.cipher_by_mask_and_frame_byte.resize(mask_size * layout->block_size);

    size_t stream_offset = 0;
    for (const auto& chunk : source.chunks()) {
        if (!chunk.is_stream() || !chunk.belongs_to(stream_id)) {
            continue;
        }
        const std::span<const uint8_t> payload = chunk.payload;
        const size_t plain_size = std::min(payload.size(), plain_prefix_size);
        for (size_t local = 0; local < plain_size; ++local) {
            const size_t absolute = stream_offset + local;
            if (absolute < layout->data_offset) {
                continue;
            }
            const size_t frame_byte = (absolute - layout->data_offset) % layout->block_size;
            ++evidence.plain_by_frame_byte[frame_byte][payload[local]];
        }

        if (payload.size() > plain_prefix_size) {
            ++evidence.audio_chunks;
            const bool zero_window = std::ranges::all_of(
                payload.subspan(zero_window_offset, mask_size),
                [](uint8_t value) { return value == 0u; }
            );
            if (zero_window && payload.size() >= plain_prefix_size + mask_size) {
                AudioMask mask;
                std::ranges::copy(
                    payload.subspan(plain_prefix_size, mask_size),
                    mask.begin()
                );
                const bool fixed_columns_match = [&] {
                    for (size_t column = 1; column < mask_size; column += 2u) {
                        if (mask[column] != fixed_audio_mask[(column >> 1u) & 3u]) {
                            return false;
                        }
                    }
                    return true;
                }();
                if (fixed_columns_match) {
                    evidence.zero_window_masks.push_back(mask);
                }
            }

            for (size_t local = plain_prefix_size; local < payload.size(); ++local) {
                const size_t absolute = stream_offset + local;
                if (absolute < layout->data_offset) {
                    continue;
                }
                const size_t column = (local - plain_prefix_size) % mask_size;
                const size_t frame_byte = (absolute - layout->data_offset) % layout->block_size;
                ++evidence.cipher_by_mask_and_frame_byte[
                    cipher_histogram_index(column, frame_byte, layout->block_size)
                ][payload[local]];
                ++evidence.column_observations[column];
            }
        }
        stream_offset += payload.size();
    }

    if (evidence.audio_chunks == 0u || evidence.column_observations[0] == 0u) {
        return std::nullopt;
    }
    return evidence;
}

[[nodiscard]] double log_likelihood(
    const TrackEvidence& evidence,
    size_t column,
    uint8_t mask
) {
    constexpr double smoothing = 1.0;
    double score = 0.0;
    for (size_t frame_byte = 0; frame_byte < evidence.block_size; ++frame_byte) {
        const auto& plain = evidence.plain_by_frame_byte[frame_byte];
        const auto& cipher = evidence.cipher_by_mask_and_frame_byte[
            cipher_histogram_index(column, frame_byte, evidence.block_size)
        ];
        uint64_t plain_total = 0;
        for (uint32_t count : plain) {
            plain_total += count;
        }
        if (plain_total == 0u) {
            continue;
        }
        const double denominator = static_cast<double>(plain_total) + 256.0 * smoothing;
        for (size_t value = 0; value < 256u; ++value) {
            if (cipher[value] == 0u) {
                continue;
            }
            const uint8_t decoded = static_cast<uint8_t>(value) ^ mask;
            const double probability = (static_cast<double>(plain[decoded]) + smoothing) / denominator;
            score += static_cast<double>(cipher[value]) * std::log(probability);
        }
    }
    return score;
}

[[nodiscard]] double score_full_mask(const TrackEvidence& evidence, const AudioMask& mask) {
    double score = 0.0;
    for (size_t column = 0; column < mask_size; ++column) {
        score += log_likelihood(evidence, column, mask[column]);
    }
    return score;
}

[[nodiscard]] double encryption_gain(const TrackEvidence& evidence) {
    double gain = 0.0;
    for (size_t column = 1; column < mask_size; column += 2u) {
        gain += log_likelihood(evidence, column, fixed_audio_mask[(column >> 1u) & 3u]);
        gain -= log_likelihood(evidence, column, 0u);
    }
    return gain;
}

[[nodiscard]] AudioMask regenerate_audio_mask(uint64_t key) {
    UsmCrypto crypto;
    crypto.init_key(key);
    return crypto.audio_mask();
}

[[nodiscard]] std::optional<AudioMask> score_zero_window_masks(const TrackEvidence& evidence) {
    std::optional<AudioMask> best;
    double best_score = -std::numeric_limits<double>::infinity();
    for (const auto& observed : evidence.zero_window_masks) {
        const uint64_t key = UsmCrypto::recover_key_from_audio_mask(observed);
        const auto mask = regenerate_audio_mask(key);
        const double score = score_full_mask(evidence, mask);
        if (score > best_score) {
            best = mask;
            best_score = score;
        }
    }
    return best;
}

[[nodiscard]] ScoredMask score_statistical_mask(const TrackEvidence& evidence) {
    ScoredMask result;
    for (size_t column = 1; column < mask_size; column += 2u) {
        result.mask[column] = fixed_audio_mask[(column >> 1u) & 3u];
    }

    double minimum_normalized_margin = std::numeric_limits<double>::infinity();
    for (size_t column = 0; column < mask_size; column += 2u) {
        double best = -std::numeric_limits<double>::infinity();
        double second = -std::numeric_limits<double>::infinity();
        uint8_t best_mask = 0;
        for (unsigned mask = 0; mask < 256u; ++mask) {
            const double score = log_likelihood(evidence, column, static_cast<uint8_t>(mask));
            if (score > best) {
                second = best;
                best = score;
                best_mask = static_cast<uint8_t>(mask);
            } else if (score > second) {
                second = score;
            }
        }
        result.mask[column] = best_mask;
        const double observations = static_cast<double>(
            std::max<size_t>(evidence.column_observations[column], 1u)
        );
        minimum_normalized_margin = std::min(
            minimum_normalized_margin,
            (best - second) / std::sqrt(observations)
        );
    }

    const uint64_t key = UsmCrypto::recover_key_from_audio_mask(result.mask);
    result.mask = regenerate_audio_mask(key);
    result.confidence = std::isfinite(minimum_normalized_margin)
        ? 1.0 - std::exp(-std::max(0.0, minimum_normalized_margin))
        : 0.0;
    return result;
}

[[nodiscard]] std::optional<AudioKeyGuess> recover_track(const TrackEvidence& evidence) {
    if (encryption_gain(evidence) <= 0.0) {
        return std::nullopt;
    }

    const auto zero_window_mask = score_zero_window_masks(evidence);
    const auto selected = zero_window_mask
        ? ScoredMask{.mask = *zero_window_mask, .confidence = 0.999}
        : score_statistical_mask(evidence);

    return AudioKeyGuess{
        .key = UsmCrypto::recover_key_from_audio_mask(selected.mask),
        .score = static_cast<float>(std::clamp(selected.confidence, 0.0, 1.0)),
        .audio_chunks = evidence.audio_chunks,
        .audio_streams = 1u,
        .used_zero_window = zero_window_mask.has_value(),
    };
}

} // namespace

std::optional<AudioKeyGuess> recover_adx_audio_key(const UsmReader& source) {
    std::vector<AudioKeyGuess> guesses;
    for (const auto& stream : source.streams()) {
        if (stream.stream_id != UsmChunkType::SFA) {
            continue;
        }
        auto evidence = collect_track(source, stream.id());
        if (!evidence) {
            continue;
        }
        auto guess = recover_track(*evidence);
        if (!guess) {
            continue;
        }
        const auto matching = std::ranges::find(guesses, guess->key, &AudioKeyGuess::key);
        if (matching == guesses.end()) {
            guesses.push_back(*guess);
            continue;
        }
        matching->score = std::max(matching->score, guess->score);
        matching->audio_chunks += guess->audio_chunks;
        matching->audio_streams += guess->audio_streams;
        matching->used_zero_window = matching->used_zero_window || guess->used_zero_window;
    }
    if (guesses.empty()) {
        return std::nullopt;
    }
    const auto better_guess = [](const AudioKeyGuess& left, const AudioKeyGuess& right) {
        if (left.used_zero_window != right.used_zero_window) {
            return left.used_zero_window;
        }
        if (left.audio_streams != right.audio_streams) {
            return left.audio_streams > right.audio_streams;
        }
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.audio_chunks > right.audio_chunks;
    };
    return *std::ranges::max_element(guesses, [&](const auto& left, const auto& right) {
        return better_guess(right, left);
    });
}

} // namespace cricodecs::usm
