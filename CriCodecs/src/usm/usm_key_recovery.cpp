/**
 * @file usm_key_recovery.cpp
 * @brief Statistical recovery of the effective USM video-mask key.
 */

#include "usm_key_recovery.hpp"
#include "usm_key_recovery_internal.hpp"

#include "usm_container.hpp"
#include "usm_crypto.hpp"

#include "../hca/hca_codec.hpp"
#include "../video/h264.hpp"
#include "../video/mpeg.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <tuple>
#include <vector>

namespace cricodecs::usm {

namespace {

constexpr size_t mask_size = 0x20;
constexpr size_t protected_tail_offset = 0x140;
constexpr size_t minimum_masked_payload_size = 0x240;
constexpr size_t pair_value_count = 1u << 16u;
constexpr size_t beam_width = 512;
constexpr uint64_t embedded_hca_support_numerator = 9;
constexpr uint64_t embedded_hca_support_denominator = 10;
constexpr size_t structure_probe_count = 8;
constexpr size_t structure_probe_size = 4u * 1024u;

using Mask = detail::UsmMask;

struct Evidence {
    std::array<std::array<uint32_t, 256>, mask_size> bytes{};
    std::vector<uint32_t> pairs = std::vector<uint32_t>((mask_size - 1u) * pair_value_count);
    uint64_t keyless_zero_bytes = 0;
    uint64_t keyless_ff_bytes = 0;
    uint64_t keyless_zero_pairs = 0;
    uint64_t keyless_ff_pairs = 0;
    size_t sample_blocks = 0;
    size_t video_chunks = 0;
};

struct Weights {
    uint32_t zero_byte = 1;
    uint32_t ff_byte = 1;
    uint32_t zero_pair = 2;
    uint32_t ff_pair = 2;
};

struct Candidate {
    std::array<uint8_t, 7> seed{};
    uint64_t score = 0;
};

struct ScoreTerms {
    std::array<uint8_t, mask_size> byte_columns{};
    std::array<uint8_t, mask_size - 1u> pair_columns{};
    uint8_t byte_count = 0;
    uint8_t pair_count = 0;
};

constexpr std::array<uint8_t, mask_size> dependency_mask = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x01,
    0x06, 0x03, 0x04, 0x02, 0x03, 0x0E, 0x0E, 0x06,
    0x06, 0x07, 0x06, 0x08, 0x10, 0x20, 0x40, 0x0E,
    0x21, 0x08, 0x1E, 0x40, 0x0E, 0x18, 0x60, 0x18,
};

[[nodiscard]] constexpr uint8_t add8(uint8_t lhs, uint8_t rhs) noexcept {
    return static_cast<uint8_t>(static_cast<unsigned>(lhs) + static_cast<unsigned>(rhs));
}

[[nodiscard]] constexpr uint8_t sub8(uint8_t lhs, uint8_t rhs) noexcept {
    return static_cast<uint8_t>(static_cast<unsigned>(lhs) - static_cast<unsigned>(rhs));
}

[[nodiscard]] constexpr uint64_t effective_key(const std::array<uint8_t, 7>& seed) noexcept {
    const uint32_t lower =
        static_cast<uint32_t>(seed[0]) |
        (static_cast<uint32_t>(seed[1]) << 8u) |
        (static_cast<uint32_t>(seed[2]) << 16u) |
        (static_cast<uint32_t>(add8(seed[3], 0x34u)) << 24u);
    const uint32_t upper =
        static_cast<uint32_t>(sub8(seed[4], 0xF9u)) |
        (static_cast<uint32_t>(seed[5] ^ 0x13u) << 8u) |
        (static_cast<uint32_t>(sub8(seed[6], 0x61u)) << 16u);
    return static_cast<uint64_t>(lower) | (static_cast<uint64_t>(upper) << 32u);
}

[[nodiscard]] constexpr bool is_video_chunk(const UsmChunk& chunk) noexcept {
    return chunk.is_stream() &&
        (chunk.chunk_type() == UsmChunkType::SFV || chunk.chunk_type() == UsmChunkType::ALP);
}

void collect_keyless_block(Evidence& evidence, std::span<const uint8_t, mask_size> block) noexcept {
    for (const uint8_t value : block) {
        evidence.keyless_zero_bytes += value == 0;
        evidence.keyless_ff_bytes += value == 0xFFu;
    }
    for (size_t column = 0; column + 1u < mask_size; ++column) {
        evidence.keyless_zero_pairs += block[column] == 0 && block[column + 1u] == 0;
        evidence.keyless_ff_pairs += block[column] == 0xFFu && block[column + 1u] == 0xFFu;
    }
}

void collect_masked_block(Evidence& evidence, std::span<const uint8_t, mask_size> block) noexcept {
    for (size_t column = 0; column < mask_size; ++column) {
        ++evidence.bytes[column][block[column]];
    }
    for (size_t column = 0; column + 1u < mask_size; ++column) {
        const uint16_t pair = static_cast<uint16_t>(
            (static_cast<uint16_t>(block[column]) << 8u) | block[column + 1u]
        );
        ++evidence.pairs[column * pair_value_count + pair];
    }
    ++evidence.sample_blocks;
}

void collect_chunk(Evidence& evidence, std::span<const uint8_t> payload) {
    if (payload.size() <= minimum_masked_payload_size) {
        return;
    }

    const auto tail = payload.subspan(protected_tail_offset);
    const size_t block_count = tail.size() / mask_size;
    if (block_count == 0) {
        return;
    }

    std::array<uint8_t, mask_size> prefix_xor{};
    bool contributed = false;
    for (size_t block_index = 0; block_index < block_count; ++block_index) {
        const auto block = tail.subspan(block_index * mask_size, mask_size);
        for (size_t column = 0; column < mask_size; ++column) {
            prefix_xor[column] ^= block[column];
        }

        if ((block_index & 1u) != 0) {
            collect_keyless_block(evidence, prefix_xor);
            continue;
        }

        collect_masked_block(evidence, prefix_xor);
        contributed = true;
    }

    std::array<std::array<uint8_t, mask_size>, 2> parity_xor{};
    for (size_t block_index = 0; block_index < 8u; ++block_index) {
        const auto encrypted_tail = tail.subspan(block_index * mask_size, mask_size);
        auto& parity = parity_xor[block_index & 1u];
        std::array<uint8_t, mask_size> relation{};
        const auto encrypted_head = payload.subspan(0x40u + block_index * mask_size, mask_size);
        for (size_t column = 0; column < mask_size; ++column) {
            parity[column] ^= encrypted_tail[column];
            relation[column] = static_cast<uint8_t>(encrypted_head[column] ^ parity[column] ^ 0xFFu);
        }

        if (block_index == 0u || block_index == 1u || block_index == 4u || block_index == 5u) {
            collect_keyless_block(evidence, relation);
        } else {
            collect_masked_block(evidence, relation);
        }
    }
    evidence.video_chunks += contributed;
}

[[nodiscard]] Evidence collect_evidence(const UsmReader& source) {
    Evidence evidence;
    for (const auto& chunk : source.chunks()) {
        if (is_video_chunk(chunk)) {
            collect_chunk(evidence, chunk.payload);
        }
    }
    return evidence;
}

[[nodiscard]] constexpr uint32_t prior_weight(uint64_t own, uint64_t other, uint32_t base) noexcept {
    const uint64_t total = own + other;
    if (total == 0) {
        return base;
    }
    return base + static_cast<uint32_t>((static_cast<uint64_t>(base) * own) / total);
}

[[nodiscard]] Weights make_weights(const Evidence& evidence) noexcept {
    return Weights{
        .zero_byte = prior_weight(evidence.keyless_zero_bytes, evidence.keyless_ff_bytes, 128),
        .ff_byte = prior_weight(evidence.keyless_ff_bytes, evidence.keyless_zero_bytes, 128),
        .zero_pair = prior_weight(evidence.keyless_zero_pairs, evidence.keyless_ff_pairs, 256),
        .ff_pair = prior_weight(evidence.keyless_ff_pairs, evidence.keyless_zero_pairs, 256),
    };
}

[[nodiscard]] constexpr ScoreTerms make_score_terms(uint8_t before, uint8_t after) noexcept {
    ScoreTerms terms;
    for (size_t column = 0; column < mask_size; ++column) {
        const uint8_t dependencies = dependency_mask[column];
        if ((dependencies & after) == dependencies && (dependencies & before) != dependencies) {
            terms.byte_columns[terms.byte_count++] = static_cast<uint8_t>(column);
        }
    }
    for (size_t column = 0; column + 1u < mask_size; ++column) {
        const uint8_t dependencies = static_cast<uint8_t>(dependency_mask[column] | dependency_mask[column + 1u]);
        if ((dependencies & after) == dependencies && (dependencies & before) != dependencies) {
            terms.pair_columns[terms.pair_count++] = static_cast<uint8_t>(column);
        }
    }
    return terms;
}

[[nodiscard]] uint64_t score_terms(
    const Evidence& evidence,
    const Weights& weights,
    const Mask& table,
    const ScoreTerms& terms
) noexcept {
    uint64_t score = 0;
    for (size_t index = 0; index < terms.byte_count; ++index) {
        const size_t column = terms.byte_columns[index];
        const uint8_t mask = table[column];
        score += static_cast<uint64_t>(weights.zero_byte) * evidence.bytes[column][mask ^ 0xFFu];
        score += static_cast<uint64_t>(weights.ff_byte) * evidence.bytes[column][mask];
    }

    for (size_t index = 0; index < terms.pair_count; ++index) {
        const size_t column = terms.pair_columns[index];
        const uint16_t zero_pair = static_cast<uint16_t>(
            (static_cast<uint16_t>(table[column] ^ 0xFFu) << 8u) |
            static_cast<uint8_t>(table[column + 1u] ^ 0xFFu)
        );
        const uint16_t ff_pair = static_cast<uint16_t>(
            (static_cast<uint16_t>(table[column]) << 8u) | table[column + 1u]
        );
        const size_t base = column * pair_value_count;
        score += static_cast<uint64_t>(weights.zero_pair) * evidence.pairs[base + zero_pair];
        score += static_cast<uint64_t>(weights.ff_pair) * evidence.pairs[base + ff_pair];
    }
    return score;
}

[[nodiscard]] bool better_candidate(const Candidate& lhs, const Candidate& rhs) noexcept {
    if (lhs.score != rhs.score) {
        return lhs.score > rhs.score;
    }
    return lhs.seed < rhs.seed;
}

void retain_best(std::vector<Candidate>& candidates) {
    if (candidates.size() > beam_width) {
        std::nth_element(
            candidates.begin(),
            candidates.begin() + static_cast<std::ptrdiff_t>(beam_width),
            candidates.end(),
            better_candidate
        );
        candidates.resize(beam_width);
    }
    std::ranges::sort(candidates, better_candidate);
}

[[nodiscard]] std::vector<Candidate> initial_candidates(const Evidence& evidence, const Weights& weights) {
    std::vector<Candidate> candidates;
    candidates.reserve(pair_value_count);
    constexpr uint8_t known = 0x06;
    constexpr auto terms = make_score_terms(0, known);
    for (unsigned seed1 = 0; seed1 < 256; ++seed1) {
        for (unsigned seed2 = 0; seed2 < 256; ++seed2) {
            Candidate candidate;
            candidate.seed[1] = static_cast<uint8_t>(seed1);
            candidate.seed[2] = static_cast<uint8_t>(seed2);
            candidate.score = score_terms(evidence, weights, detail::expand_video_mask(candidate.seed), terms);
            candidates.push_back(candidate);
        }
    }
    retain_best(candidates);
    return candidates;
}

void extend_candidates(
    std::vector<Candidate>& candidates,
    const Evidence& evidence,
    const Weights& weights,
    uint8_t seed_index,
    uint8_t before
) {
    const uint8_t after = static_cast<uint8_t>(before | (1u << seed_index));
    const auto terms = make_score_terms(before, after);
    std::vector<Candidate> expanded;
    expanded.reserve(candidates.size() * 256u);
    for (const auto& prefix : candidates) {
        for (unsigned value = 0; value < 256; ++value) {
            Candidate candidate = prefix;
            candidate.seed[seed_index] = static_cast<uint8_t>(value);
            candidate.score += score_terms(
                evidence,
                weights,
                detail::expand_video_mask(candidate.seed),
                terms
            );
            expanded.push_back(candidate);
        }
    }
    retain_best(expanded);
    candidates = std::move(expanded);
}

[[nodiscard]] uint64_t maximum_score(const Evidence& evidence, const Weights& weights) noexcept {
    const uint64_t byte_weight = std::max(weights.zero_byte, weights.ff_byte);
    const uint64_t pair_weight = std::max(weights.zero_pair, weights.ff_pair);
    return static_cast<uint64_t>(evidence.sample_blocks) *
        (mask_size * byte_weight + (mask_size - 1u) * pair_weight);
}

struct EmbeddedHcaGuess {
    uint64_t key = 0;
    float score = 0.0f;
    size_t stream_count = 0;
    bool video_supported = false;
};

[[nodiscard]] std::optional<EmbeddedHcaGuess> recover_embedded_hca_key(const UsmReader& source) {
    std::vector<hca::Hca> audio;
    for (const auto& stream : source.streams()) {
        if (stream.stream_id != UsmChunkType::SFA) {
            continue;
        }
        std::vector<uint8_t> bytes;
        for (const auto& chunk : source.chunks()) {
            if (chunk.is_stream() && chunk.belongs_to(stream.id())) {
                bytes.insert(bytes.end(), chunk.payload.begin(), chunk.payload.end());
            }
        }
        auto parsed = hca::Hca::load(bytes);
        if (parsed && parsed->header().cipher.type == 56u) {
            audio.push_back(std::move(*parsed));
        }
    }
    if (audio.empty()) {
        return std::nullopt;
    }
    auto guess = hca::recover_key(audio);
    if (!guess) {
        return std::nullopt;
    }
    return guess->candidates.empty()
        ? std::nullopt
        : std::optional<EmbeddedHcaGuess>(EmbeddedHcaGuess{
            .key = guess->candidates.front().key,
            .score = guess->candidates.front().score,
            .stream_count = audio.size(),
        });
}

[[nodiscard]] bool add_external_candidate(
    std::vector<Candidate>& candidates,
    uint64_t key,
    const Evidence& evidence,
    const Weights& weights
) {
    Candidate candidate{.seed = detail::seed_from_key(key)};
    constexpr auto all_terms = make_score_terms(0, 0x7Fu);
    candidate.score = score_terms(evidence, weights, detail::expand_video_mask(candidate.seed), all_terms);
    const bool video_supported = candidate.score * embedded_hca_support_denominator >=
        candidates.front().score * embedded_hca_support_numerator;
    auto position = std::ranges::find(candidates, candidate.seed, &Candidate::seed);
    if (position == candidates.end()) {
        candidates.push_back(candidate);
    }
    std::ranges::sort(candidates, better_candidate);
    if (video_supported) {
        position = std::ranges::find(candidates, candidate.seed, &Candidate::seed);
        if (position != candidates.end()) {
            std::iter_swap(candidates.begin(), position);
        }
    }
    return video_supported;
}

[[nodiscard]] bool has_vp9_stream(const UsmReader& source) noexcept {
    return std::ranges::any_of(source.chunks(), [](const UsmChunk& chunk) {
        return is_video_chunk(chunk) && chunk.payload.size() >= 4u &&
            chunk.payload[0] == 'D' && chunk.payload[1] == 'K' &&
            chunk.payload[2] == 'I' && chunk.payload[3] == 'F';
    });
}

struct Vp9Probe {
    std::vector<uint8_t> encrypted;
    std::vector<uint8_t> tail_prefix;
    std::array<std::array<uint8_t, mask_size>, 8> head_relation{};
    size_t frame_offset = 0;
    size_t frame_size = 0;
    size_t masked_end = 0;
    bool masked = false;
};

[[nodiscard]] std::vector<Vp9Probe> make_vp9_probes(const UsmReader& source) {
    std::vector<Vp9Probe> probes;
    for (const auto& chunk : source.chunks()) {
        if (!is_video_chunk(chunk) || chunk.chunk_type() != UsmChunkType::SFV || chunk.payload.size() < 12u) {
            continue;
        }

        Vp9Probe probe;
        const size_t payload_size = chunk.payload.size();
        probe.encrypted.reserve(chunk.payload.size() + chunk.padding.size());
        probe.encrypted.insert(probe.encrypted.end(), chunk.payload.begin(), chunk.payload.end());
        probe.encrypted.insert(probe.encrypted.end(), chunk.padding.begin(), chunk.padding.end());
        probe.masked = probe.encrypted.size() > minimum_masked_payload_size;
        probe.masked_end = probe.encrypted.size() <= 0x40u
            ? probe.encrypted.size()
            : 0x40u + ((probe.encrypted.size() - 0x40u) / 8u) * 8u;

        const size_t record_offset = probe.encrypted.size() >= 32u && probe.encrypted[0] == 'D' &&
            probe.encrypted[1] == 'K' && probe.encrypted[2] == 'I' && probe.encrypted[3] == 'F' ? 32u : 0u;
        if (record_offset + 12u > payload_size) {
            continue;
        }
        probe.frame_size = io::read_le<uint32_t>(probe.encrypted.data() + record_offset);
        probe.frame_offset = record_offset + 12u;
        if (probe.frame_size == 0 || probe.frame_size > payload_size - probe.frame_offset) {
            continue;
        }

        probe.tail_prefix.resize(probe.encrypted.size());
        if (probe.masked_end > protected_tail_offset) {
            std::array<uint8_t, mask_size> prefix{};
            for (size_t offset = protected_tail_offset; offset < probe.masked_end; ++offset) {
                const size_t column = (offset - protected_tail_offset) % mask_size;
                prefix[column] ^= probe.encrypted[offset];
                probe.tail_prefix[offset] = prefix[column];
            }
        }

        if (probe.encrypted.size() >= protected_tail_offset + 8u * mask_size) {
            std::array<std::array<uint8_t, mask_size>, 2> parity{};
            for (size_t block = 0; block < 8u; ++block) {
                for (size_t column = 0; column < mask_size; ++column) {
                    parity[block & 1u][column] ^=
                        probe.encrypted[protected_tail_offset + block * mask_size + column];
                    probe.head_relation[block][column] = static_cast<uint8_t>(
                        probe.encrypted[0x40u + block * mask_size + column] ^ parity[block & 1u][column]
                    );
                }
            }
        }
        probes.push_back(std::move(probe));
    }
    return probes;
}

[[nodiscard]] uint8_t probe_plain_byte(const Vp9Probe& probe, const Mask& mask, size_t offset) noexcept {
    if (offset >= probe.encrypted.size()) {
        return 0;
    }
    if (!probe.masked || offset < 0x40u || offset >= probe.masked_end) {
        return probe.encrypted[offset];
    }
    if (offset < protected_tail_offset) {
        const size_t block = (offset - 0x40u) / mask_size;
        const size_t column = (offset - 0x40u) % mask_size;
        const bool keyless = block == 0u || block == 1u || block == 4u || block == 5u;
        return static_cast<uint8_t>(
            probe.head_relation[block][column] ^ (keyless ? 0xFFu : mask[column])
        );
    }

    const size_t relative = offset - protected_tail_offset;
    const size_t block = relative / mask_size;
    const size_t column = relative % mask_size;
    return static_cast<uint8_t>(
        probe.tail_prefix[offset] ^ ((block & 1u) == 0 ? static_cast<uint8_t>(mask[column] ^ 0xFFu) : 0u)
    );
}

[[nodiscard]] bool valid_vp9_header_byte(uint8_t byte) noexcept {
    if ((byte >> 6u) != 0x02u) {
        return false;
    }
    const uint8_t profile = static_cast<uint8_t>(((byte >> 5u) & 0x01u) | ((byte >> 3u) & 0x02u));
    return profile != 3u || (byte & 0x08u) == 0;
}

[[nodiscard]] uint32_t vp9_probe_score(const Vp9Probe& probe, const Mask& mask) noexcept {
    if (!valid_vp9_header_byte(probe_plain_byte(probe, mask, probe.frame_offset))) {
        return 0;
    }

    const size_t frame_end = probe.frame_offset + probe.frame_size;
    const uint8_t marker = probe_plain_byte(probe, mask, frame_end - 1u);
    if ((marker & 0xE0u) != 0xC0u) {
        return 1;
    }

    const size_t frame_count = static_cast<size_t>(marker & 0x07u) + 1u;
    const size_t magnitude = static_cast<size_t>((marker >> 3u) & 0x03u) + 1u;
    const size_t index_size = 2u + frame_count * magnitude;
    if (index_size > probe.frame_size) {
        return 0;
    }
    const size_t index_offset = frame_end - index_size;
    if (probe_plain_byte(probe, mask, index_offset) != marker) {
        return 0;
    }

    size_t subframe_offset = probe.frame_offset;
    size_t size_offset = index_offset + 1u;
    for (size_t frame = 0; frame < frame_count; ++frame) {
        size_t subframe_size = 0;
        for (size_t byte = 0; byte < magnitude; ++byte) {
            subframe_size |= static_cast<size_t>(probe_plain_byte(probe, mask, size_offset++)) << (byte * 8u);
        }
        if (subframe_size == 0 || subframe_size > index_offset - subframe_offset ||
            !valid_vp9_header_byte(probe_plain_byte(probe, mask, subframe_offset))) {
            return 0;
        }
        subframe_offset += subframe_size;
    }
    return subframe_offset == index_offset ? static_cast<uint32_t>(frame_count) : 0u;
}

[[nodiscard]] uint32_t vp9_candidate_score(std::span<const Vp9Probe> probes, const Mask& mask) noexcept {
    uint32_t score = 0;
    for (const auto& probe : probes) {
        score += vp9_probe_score(probe, mask);
    }
    return score;
}

void rerank_vp9_candidates(std::vector<Candidate>& candidates, const UsmReader& source) {
    if (!has_vp9_stream(source)) {
        return;
    }

    const auto probes = make_vp9_probes(source);
    if (probes.empty()) {
        return;
    }

    size_t best_index = 0;
    uint32_t best_score = 0;
    for (size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        const uint32_t score = vp9_candidate_score(probes, detail::expand_video_mask(candidate.seed));
        if (score > best_score ||
            (score == best_score &&
             better_candidate(candidates[index], candidates[best_index]))) {
            best_index = index;
            best_score = score;
        }
    }
    if (best_score != 0 && best_index != 0) {
        std::swap(candidates[0], candidates[best_index]);
    }
}

enum class VideoGrammar : uint8_t {
    unknown,
    h264,
    mpeg,
};

[[nodiscard]] VideoGrammar detect_video_grammar(const UsmReader& source) noexcept {
    for (const auto& chunk : source.chunks()) {
        if (!is_video_chunk(chunk) || chunk.chunk_type() != UsmChunkType::SFV) {
            continue;
        }
        const auto bytes = std::span<const uint8_t>(chunk.payload).first(
            std::min<size_t>(chunk.payload.size(), 0x40u)
        );
        for (size_t offset = 0; offset + 4u <= bytes.size(); ++offset) {
            if (bytes[offset] != 0 || bytes[offset + 1u] != 0) {
                continue;
            }
            const bool four_byte = offset + 5u <= bytes.size() &&
                bytes[offset + 2u] == 0 && bytes[offset + 3u] == 1;
            const bool three_byte = bytes[offset + 2u] == 1;
            if (!four_byte && !three_byte) {
                continue;
            }
            const size_t header_offset = offset + (four_byte ? 4u : 3u);
            const uint8_t header = bytes[header_offset];
            if (header == 0xB3u || header == 0xB8u || header == 0xB5u || header == 0x00u) {
                return VideoGrammar::mpeg;
            }
            if ((header & 0x80u) == 0 && (header & 0x1Fu) >= 1u && (header & 0x1Fu) <= 23u) {
                return VideoGrammar::h264;
            }
        }
    }
    return VideoGrammar::unknown;
}

[[nodiscard]] std::vector<std::vector<uint8_t>> make_structure_probes(const UsmReader& source) {
    std::vector<std::vector<uint8_t>> probes;
    probes.reserve(structure_probe_count);
    for (const auto& chunk : source.chunks()) {
        if (!is_video_chunk(chunk) || chunk.chunk_type() != UsmChunkType::SFV || chunk.payload.empty()) {
            continue;
        }
        const size_t encrypted_size = chunk.payload.size() + chunk.padding.size();
        const size_t probe_size = std::min(encrypted_size, structure_probe_size);
        std::vector<uint8_t> probe;
        probe.reserve(probe_size);
        const size_t payload_size = std::min(chunk.payload.size(), probe_size);
        probe.insert(probe.end(), chunk.payload.begin(), chunk.payload.begin() + static_cast<std::ptrdiff_t>(payload_size));
        const size_t padding_size = probe_size - payload_size;
        probe.insert(probe.end(), chunk.padding.begin(), chunk.padding.begin() + static_cast<std::ptrdiff_t>(padding_size));
        probes.push_back(std::move(probe));
        if (probes.size() == structure_probe_count) {
            break;
        }
    }
    return probes;
}

struct H264CandidateStructure {
    uint32_t slice_headers = 0;
    uint32_t prevention_bytes = 0;
    uint32_t nal_headers = 0;
    uint32_t violations = 0;
    uint32_t nal_units = 0;
};

struct MpegCandidateStructure {
    uint32_t slices = 0;
    uint32_t pictures = 0;
    uint32_t start_codes = 0;
    uint32_t violations = 0;
};

[[nodiscard]] bool better_structure(const H264CandidateStructure& lhs, const H264CandidateStructure& rhs) noexcept {
    return std::tie(lhs.prevention_bytes, lhs.slice_headers, lhs.nal_headers, rhs.violations, lhs.nal_units) >
        std::tie(rhs.prevention_bytes, rhs.slice_headers, rhs.nal_headers, lhs.violations, rhs.nal_units);
}

[[nodiscard]] bool better_structure(const MpegCandidateStructure& lhs, const MpegCandidateStructure& rhs) noexcept {
    return std::tie(lhs.slices, lhs.pictures, lhs.start_codes, rhs.violations) >
        std::tie(rhs.slices, rhs.pictures, rhs.start_codes, lhs.violations);
}

void rerank_structured_video_candidates(std::vector<Candidate>& candidates, const UsmReader& source) {
    const auto grammar = detect_video_grammar(source);
    if (grammar == VideoGrammar::unknown || has_vp9_stream(source)) {
        return;
    }
    const auto encrypted_probes = make_structure_probes(source);
    if (encrypted_probes.empty()) {
        return;
    }

    size_t best_index = 0;
    H264CandidateStructure baseline_h264;
    H264CandidateStructure best_h264;
    MpegCandidateStructure best_mpeg;
    std::vector<uint8_t> plain;
    plain.reserve(structure_probe_size);
    for (size_t index = 0; index < candidates.size(); ++index) {
        UsmCrypto crypto;
        crypto.init_key(effective_key(candidates[index].seed));
        H264CandidateStructure h264;
        MpegCandidateStructure mpeg;
        for (const auto& encrypted : encrypted_probes) {
            plain.assign(encrypted.begin(), encrypted.end());
            crypto.decrypt_video(plain);
            if (grammar == VideoGrammar::h264) {
                const auto structure = video::inspect_h264_structure(plain);
                h264.slice_headers += structure.valid_slice_headers;
                h264.prevention_bytes += structure.emulation_prevention_bytes;
                h264.nal_headers += structure.valid_nal_headers;
                h264.violations += structure.ebsp_violations;
                h264.nal_units += structure.nal_units;
            } else {
                const auto structure = video::inspect_mpeg_structure(plain);
                mpeg.slices += structure.slices;
                mpeg.pictures += structure.pictures;
                mpeg.start_codes += structure.valid_start_codes;
                mpeg.violations += structure.violations;
            }
        }

        if (index == 0) {
            baseline_h264 = h264;
            best_h264 = h264;
            best_mpeg = mpeg;
            continue;
        }
        const bool h264_preserves_baseline = h264.violations <= baseline_h264.violations &&
            h264.slice_headers >= baseline_h264.slice_headers &&
            h264.nal_headers >= baseline_h264.nal_headers;
        if ((grammar == VideoGrammar::h264 && h264_preserves_baseline && better_structure(h264, best_h264)) ||
            (grammar == VideoGrammar::mpeg && better_structure(mpeg, best_mpeg))) {
            best_index = index;
            best_h264 = h264;
            best_mpeg = mpeg;
        }
    }
    if (best_index != 0) {
        std::swap(candidates[0], candidates[best_index]);
    }
}

} // namespace

std::expected<KeyRecoveryResult, std::string> recover_key(const UsmReader& source) {
    const auto audio_guess = recover_adx_audio_key(source);
    auto evidence = collect_evidence(source);
    if (evidence.sample_blocks == 0) {
        if (!audio_guess) {
            return std::unexpected(
                "USM key recovery failed: no masked-size video chunks or recoverable masked ADX audio were found"
            );
        }
        return KeyRecoveryResult{
            .candidates = {KeyCandidate{
                .key = audio_guess->key,
                .score = audio_guess->score,
                .source_count = 1,
                .evidence_count = audio_guess->audio_chunks,
                .sample_blocks = 0,
                .video_chunks = 0,
                .audio_chunks = audio_guess->audio_chunks,
                .audio_score = audio_guess->score,
            }},
            .source_count = 1,
            .evidence_count = audio_guess->audio_chunks,
        };
    }

    const auto weights = make_weights(evidence);
    auto candidates = initial_candidates(evidence, weights);
    uint8_t known = 0x06;
    constexpr std::array<uint8_t, 5> seed_indices{0, 3, 4, 6, 5};
    for (const uint8_t seed_index : seed_indices) {
        extend_candidates(candidates, evidence, weights, seed_index, known);
        known = static_cast<uint8_t>(known | (1u << seed_index));
    }
    if (candidates.empty()) {
        return std::unexpected("USM key recovery failed: candidate ranking produced no result");
    }

    if (audio_guess) {
        (void)add_external_candidate(candidates, audio_guess->key, evidence, weights);
    }
    auto hca_guess = recover_embedded_hca_key(source);
    if (hca_guess) {
        hca_guess->video_supported = add_external_candidate(candidates, hca_guess->key, evidence, weights);
    }
    rerank_vp9_candidates(candidates, source);
    rerank_structured_video_candidates(candidates, source);

    const uint64_t scale = maximum_score(evidence, weights);
    std::vector<KeyCandidate> recovered;
    recovered.reserve(std::min(candidates.size(), MaxKeyRecoveryCandidates));
    for (const auto& candidate : candidates) {
        const uint64_t key = effective_key(candidate.seed);
        if (std::ranges::contains(recovered, key, &KeyCandidate::key)) {
            continue;
        }
        const bool matches_audio = audio_guess && key == audio_guess->key;
        recovered.push_back(KeyCandidate{
            .key = key,
            .score = scale == 0 ? 0.0f : static_cast<float>(
                static_cast<double>(candidate.score) / static_cast<double>(scale)),
            .source_count = 1,
            .evidence_count = evidence.sample_blocks +
                (matches_audio ? audio_guess->audio_chunks : 0u),
            .sample_blocks = evidence.sample_blocks,
            .video_chunks = evidence.video_chunks,
            .audio_chunks = matches_audio ? audio_guess->audio_chunks : 0u,
            .audio_score = matches_audio ? audio_guess->score : 0.0f,
            .hca_streams = hca_guess && key == hca_guess->key ? hca_guess->stream_count : 0u,
            .hca_score = hca_guess && key == hca_guess->key ? hca_guess->score : 0.0f,
            .hca_video_supported = hca_guess && key == hca_guess->key && hca_guess->video_supported,
        });
        if (recovered.size() == MaxKeyRecoveryCandidates) {
            break;
        }
    }
    return KeyRecoveryResult{
        .candidates = std::move(recovered),
        .source_count = 1,
        .evidence_count = evidence.sample_blocks +
            (audio_guess ? audio_guess->audio_chunks : 0u),
    };
}

} // namespace cricodecs::usm
