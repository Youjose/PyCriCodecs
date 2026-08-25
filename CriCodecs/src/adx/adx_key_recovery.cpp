#include "adx_key_recovery.hpp"
#include "adx_key_recovery_internal.hpp"

#include "adx_codec.hpp"

#include "../utilities/io_endian.hpp"
#include "../utilities/numeric.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

namespace cricodecs::adx {

namespace {

constexpr uint16_t Type8Mask = 0x6000u;
constexpr size_t Type8ConfidenceFrames = 128;
constexpr size_t Type9CycleFrames = 8192;
constexpr size_t UnlimitedFrames = std::numeric_limits<size_t>::max();

struct ParsedFrame {
    uint16_t scale;
    bool nonempty;
};

using ParsedSource = std::vector<ParsedFrame>;

[[nodiscard]] std::expected<ParsedSource, std::string> parse_source(
    AdxRecoverySource source, uint8_t expected_type) {
    if (source.bytes.size() < 32u || io::read_be<uint16_t>(source.bytes.data()) != 0x8000u) {
        return std::unexpected("ADX recovery: invalid ADX header");
    }
    const uint8_t frame_size = source.bytes[5];
    const uint8_t channels = source.bytes[7];
    const uint8_t flags = source.bytes[19];
    if (frame_size < 2u || channels == 0u || (flags != 8u && flags != 9u)) {
        return std::unexpected("ADX recovery: unsupported encrypted ADX header");
    }
    if (flags != expected_type) {
        return std::unexpected("ADX recovery: mixed encryption types");
    }
    const uint32_t sample_count = io::read_be<uint32_t>(source.bytes.data() + 12u);
    const uint32_t samples_per_block = static_cast<uint32_t>(frame_size - 2u) * 2u;
    const size_t frame_count = static_cast<size_t>(
        util::divide_round_up(sample_count, samples_per_block)) * channels;
    const size_t audio_offset = static_cast<size_t>(io::read_be<uint16_t>(source.bytes.data() + 2u)) + 4u;
    if (audio_offset > source.bytes.size() || frame_count > (source.bytes.size() - audio_offset) / frame_size) {
        return std::unexpected("ADX recovery: audio frames exceed input");
    }
    ParsedSource result;
    result.reserve(frame_count);
    for (size_t frame = 0; frame < frame_count; ++frame) {
        const size_t offset = audio_offset + frame * frame_size;
        const auto bytes = source.bytes.subspan(offset, frame_size);
        result.push_back({
            .scale = io::read_be<uint16_t>(bytes.data()),
            .nonempty = std::ranges::any_of(bytes, [](uint8_t value) { return value != 0u; }),
        });
    }
    return result;
}

[[nodiscard]] std::expected<std::vector<ParsedSource>, std::string> parse_sources(
    std::span<const AdxRecoverySource> sources, uint8_t expected_type) {
    std::vector<ParsedSource> parsed;
    parsed.reserve(sources.size());
    for (const auto source : sources) {
        auto parsed_source = parse_source(source, expected_type);
        if (!parsed_source) return std::unexpected(parsed_source.error());
        parsed.push_back(std::move(*parsed_source));
    }
    return parsed;
}

struct ValidationMetrics {
    uint64_t examined = 0;
    uint64_t evidence = 0;
};

[[nodiscard]] std::optional<ValidationMetrics> validate_candidate(
    const AdxKeyState key, std::span<const ParsedSource> sources,
    uint16_t mask, size_t evidence_limit) {
    ValidationMetrics result;
    for (const auto& source : sources) {
        AdxKeyState state = key;
        for (const auto frame : source) {
            ++result.examined;
            if (frame.nonempty) {
                ++result.evidence;
                if (((frame.scale ^ state.xor_value) & mask) != 0u) {
                    return std::nullopt;
                }
            }
            state.advance();
            if (result.evidence >= evidence_limit) {
                return result;
            }
        }
    }
    return result;
}

[[nodiscard]] std::vector<uint64_t> source_frame_counts(
    std::span<const ParsedSource> sources) {
    std::vector<uint64_t> counts;
    counts.reserve(sources.size());
    for (const auto& source : sources) {
        counts.push_back(source.size());
    }
    return counts;
}

[[nodiscard]] uint64_t total_frame_count(std::span<const ParsedSource> sources) {
    uint64_t total = 0;
    for (const auto& source : sources) {
        total += source.size();
    }
    return total;
}

[[nodiscard]] uint64_t evidence_frame_count(std::span<const ParsedSource> sources) {
    uint64_t total = 0;
    for (const auto& source : sources) {
        total += std::ranges::count_if(source,
            [](const ParsedFrame frame) { return frame.nonempty; });
    }
    return total;
}

void add_result_candidate(AdxRecoveryResult& result) {
    result.candidates.push_back(AdxKeyCandidate{
        .key = result.key,
        .score = result.score,
        .source_count = result.source_count,
        .evidence_count = result.evidence_frames,
        .evidence_frames = result.evidence_frames,
        .canonical_type9_code = result.canonical_type9_code,
    });
}

template <typename Worker>
void run_recovery_workers(Worker worker) {
    const unsigned worker_count = std::max(
        1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::jthread> workers;
    workers.reserve(worker_count);
    for (unsigned index = 0; index < worker_count; ++index) {
        workers.emplace_back(worker);
    }
}

} // namespace

std::expected<AdxRecoveryResult, std::string> recover_key(
    std::span<const AdxRecoverySource> sources) {
    if (sources.empty()) {
        return std::unexpected("ADX recovery: no sources");
    }
    if (sources.front().bytes.size() > 19u && sources.front().bytes[19] == 9u) {
        return recover_key_type9(sources);
    }
    auto parsed_result = parse_sources(sources, 8u);
    if (!parsed_result) return std::unexpected(parsed_result.error());
    auto& parsed = *parsed_result;

    const auto& first = parsed.front();
    if (first.empty()) {
        return std::unexpected("ADX recovery: no audio frames");
    }

    struct RecoveredCandidate {
        AdxKeyState key;
        ValidationMetrics validation;
    };
    std::optional<RecoveredCandidate> recovered;
    std::mutex recovered_mutex;
    std::atomic<size_t> next_seed{0};
    std::atomic<bool> found{false};
    run_recovery_workers([&] {
        while (!found.load(std::memory_order_relaxed)) {
            const size_t seed_index = next_seed.fetch_add(1, std::memory_order_relaxed);
            if (seed_index >= KEY8_PRIMES.size()) {
                return;
            }
            const uint16_t seed = KEY8_PRIMES[seed_index];
            if (first.front().scale != 0u &&
                ((first.front().scale ^ seed) & Type8Mask) != 0u) {
                continue;
            }
            for (const uint16_t mult : KEY8_PRIMES) {
                for (const uint16_t add : KEY8_PRIMES) {
                    const AdxKeyState candidate{seed, mult, add};
                    if (!validate_candidate(
                            candidate, parsed, Type8Mask, Type8ConfidenceFrames)) {
                        continue;
                    }
                    const auto full_validation = validate_candidate(
                        candidate, parsed, Type8Mask, UnlimitedFrames);
                    if (!full_validation) {
                        continue;
                    }
                    if (!found.exchange(true, std::memory_order_relaxed)) {
                        std::lock_guard lock(recovered_mutex);
                        recovered = RecoveredCandidate{candidate, *full_validation};
                    }
                    return;
                }
                if (found.load(std::memory_order_relaxed)) {
                    return;
                }
            }
        }
    });
    if (recovered) {
        AdxRecoveryResult result{
            .key = recovered->key,
            .encryption_type = 8u,
            .score = static_cast<float>(std::min<uint64_t>(
                recovered->validation.evidence, Type8ConfidenceFrames)) /
                static_cast<float>(Type8ConfidenceFrames),
            .examined_frames = recovered->validation.examined,
            .evidence_frames = recovered->validation.evidence,
            .total_frames = total_frame_count(parsed),
            .source_frames = source_frame_counts(parsed),
            .candidates = {},
            .source_count = sources.size(),
            .evidence_count = recovered->validation.evidence,
        };
        add_result_candidate(result);
        return result;
    }
    return std::unexpected("ADX recovery: no structurally valid type-8 triplet");
}

namespace {

constexpr size_t Type9SignatureLags = 8;
constexpr size_t Type9Modulus = 0x2000u;
constexpr size_t Type9Half = Type9Modulus / 2u;
constexpr size_t Type9Mask = Type9Modulus - 1u;
using Type9CorrelationTable = std::array<uint16_t, Type9Modulus>;
using Type9LagTables = std::array<Type9CorrelationTable, Type9SignatureLags>;

void build_high_bit_equal_table(uint16_t multiplier, Type9CorrelationTable& result) {
    // For odd multipliers, adding 0x1000 to the input flips the output high
    // bit. Count the lower half once with circular range differences, then
    // double it to obtain every offset's full-cycle equality count.
    std::array<int32_t, Type9Modulus + 1u> differences{};
    uint16_t mapped = 0;
    for (size_t state = 0; state < Type9Half; ++state) {
        const size_t begin = static_cast<size_t>(-mapped) & Type9Mask;
        const size_t end = (begin + Type9Half) & Type9Mask;
        if (begin < end) {
            ++differences[begin];
            --differences[end];
        } else {
            ++differences[begin];
            --differences[Type9Modulus];
            ++differences[0];
            --differences[end];
        }
        mapped = static_cast<uint16_t>((mapped + multiplier) & Type9Mask);
    }

    int32_t equal = 0;
    for (size_t offset = 0; offset < Type9Modulus; ++offset) {
        equal += differences[offset];
        result[offset] = static_cast<uint16_t>(equal * 2);
    }
}

struct Type9Candidate {
    uint16_t mult{};
    uint16_t add{};
};

[[nodiscard]] std::optional<uint16_t> find_type9_phase(
    const Type9Candidate candidate, std::span<const ParsedSource> sources,
    size_t primary_index) {
    std::array<uint8_t, Type9CycleFrames> cycle{};
    uint16_t state = 0;
    for (auto& bit : cycle) {
        bit = static_cast<uint8_t>((state >> 12u) & 1u);
        state = static_cast<uint16_t>((state * candidate.mult + candidate.add) & 0x1FFFu);
    }

    const auto matches = [&](const ParsedSource& source, uint16_t phase,
                             size_t begin, size_t end) {
        for (size_t frame = begin; frame < end; ++frame) {
            const auto observed = source[frame];
            if (observed.nonempty &&
                cycle[(static_cast<size_t>(phase) + frame) & (Type9CycleFrames - 1u)] !=
                    static_cast<uint8_t>((observed.scale >> 12u) & 1u)) {
                return false;
            }
        }
        return true;
    };

    const auto& primary = sources[primary_index];
    const size_t prefix = std::min<size_t>(primary.size(), 256u);
    for (uint16_t phase = 0; phase < Type9CycleFrames; ++phase) {
        if (!matches(primary, phase, 0, prefix) ||
            !matches(primary, phase, prefix, primary.size())) {
            continue;
        }
        bool all_match = true;
        for (size_t source_index = 0; source_index < sources.size(); ++source_index) {
            if (source_index != primary_index &&
                !matches(sources[source_index], phase, 0, sources[source_index].size())) {
                all_match = false;
                break;
            }
        }
        if (all_match) {
            uint16_t recovered = 0;
            for (size_t i = 0; i < phase; ++i) {
                recovered = static_cast<uint16_t>(
                    (recovered * candidate.mult + candidate.add) & 0x1FFFu);
            }
            return recovered;
        }
    }
    return std::nullopt;
}

[[nodiscard]] float type9_decode_score(
    std::span<const AdxRecoverySource> sources,
    std::span<const ParsedSource> parsed, AdxKeyState key) {
    uint64_t clipped = 0;
    uint64_t sample_count = 0;
    uint64_t evidence = 0;
    double scale_quality = 0.0;
    for (size_t source_index = 0; source_index < sources.size(); ++source_index) {
        AdxKeyState state = key;
        for (const auto frame : parsed[source_index]) {
            if (frame.nonempty) {
                const uint16_t scale = static_cast<uint16_t>(
                    (frame.scale ^ state.xor_value) & 0x0FFFu);
                scale_quality += 512.0 / (512.0 + scale);
                ++evidence;
            }
            state.advance();
        }

        auto loaded = Adx::load(sources[source_index].bytes);
        if (!loaded) {
            return -std::numeric_limits<float>::infinity();
        }
        loaded->set_key_triplet(key.xor_value, key.mult, key.add);
        auto decoded = loaded->decode();
        if (!decoded || decoded->pcm_data.empty()) {
            return -std::numeric_limits<float>::infinity();
        }
        sample_count += decoded->pcm_data.size();
        for (const int16_t sample : decoded->pcm_data) {
            clipped += std::abs(sample) >= 32767;
        }
    }
    if (evidence == 0 || sample_count == 0) {
        return 0.0f;
    }
    const double structural = std::min(
        1.0, static_cast<double>(evidence) / Type9CycleFrames);
    const double scale = scale_quality / static_cast<double>(evidence);
    const double clip = 1.0 - static_cast<double>(clipped) / sample_count;
    return static_cast<float>(std::clamp(
        0.45 * structural + 0.45 * scale + 0.10 * clip, 0.0, 1.0));
}

} // namespace

std::expected<AdxRecoveryResult, std::string> recover_key_type9(
    std::span<const AdxRecoverySource> sources) {
    if (sources.empty()) {
        return std::unexpected("ADX recovery: no sources");
    }
    auto parsed_result = parse_sources(sources, 9u);
    if (!parsed_result) return std::unexpected(parsed_result.error());
    auto& parsed = *parsed_result;
    const auto primary = static_cast<size_t>(std::distance(
        parsed.begin(), std::ranges::max_element(
            parsed, {}, [](const ParsedSource& source) { return source.size(); })));
    const auto& source = parsed[primary];
    if (source.size() < Type9CycleFrames) {
        return std::unexpected("ADX recovery: type-9 blind recovery needs at least 8192 frames");
    }

    std::array<uint64_t, Type9SignatureLags> observed{};
    for (size_t lag = 1; lag <= Type9SignatureLags; ++lag) {
        for (size_t frame = 0; frame + lag < Type9CycleFrames; ++frame) {
            if (source[frame].nonempty && source[frame + lag].nonempty &&
                ((source[frame].scale ^ source[frame + lag].scale) & 0x1000u) == 0u) {
                ++observed[lag - 1u];
            }
        }
    }
    const auto missing = static_cast<uint64_t>(std::ranges::count_if(
        source, [](const ParsedFrame frame) { return !frame.nonempty; }));
    // The observed window is not necessarily phase-aligned to a full LCG
    // cycle, so exact full-cycle correlation counts can differ by a small
    // finite-window error even when the triplet is valid.
    const uint64_t tolerance = std::max<uint64_t>(32u, 2u + missing / 4u);

    std::vector<Type9Candidate> candidates;
    std::mutex candidates_mutex;
    std::atomic<int> next_multiplier{1};
    run_recovery_workers([&] {
        std::vector<Type9Candidate> local;
        Type9LagTables correlation{};
        std::array<uint16_t, Type9SignatureLags> offset_factors{};
        for (int multiplier = next_multiplier.fetch_add(4); multiplier < 0x2000;
             multiplier = next_multiplier.fetch_add(4)) {
            uint16_t power = 1;
            uint16_t offset_factor = 0;
            for (size_t lag = 0; lag < Type9SignatureLags; ++lag) {
                offset_factor = static_cast<uint16_t>(
                    (offset_factor + power) & Type9Mask);
                power = static_cast<uint16_t>(
                    (static_cast<uint32_t>(power) * multiplier) & Type9Mask);
                offset_factors[lag] = offset_factor;
                build_high_bit_equal_table(power, correlation[lag]);
            }
            for (uint16_t add = 1; add < 0x2000; add = static_cast<uint16_t>(add + 2u)) {
                uint64_t distance = 0;
                for (size_t i = 0; i < Type9SignatureLags; ++i) {
                    const uint16_t equal = correlation[i][
                        static_cast<uint32_t>(add) * offset_factors[i] & Type9Mask];
                    distance += equal > observed[i]
                        ? equal - observed[i]
                        : observed[i] - equal;
                }
                if (distance <= tolerance) {
                    local.push_back({static_cast<uint16_t>(multiplier), add});
                }
            }
        }
        std::lock_guard lock(candidates_mutex);
        candidates.insert(candidates.end(), local.begin(), local.end());
    });

    const uint64_t total_frames = total_frame_count(parsed);
    AdxRecoveryResult best{
        .encryption_type = 9u,
        .score = -std::numeric_limits<float>::infinity(),
        .examined_frames = total_frames,
        .evidence_frames = evidence_frame_count(parsed),
        .total_frames = total_frames,
        .source_frames = source_frame_counts(parsed),
        .candidates = {},
        .source_count = sources.size(),
    };
    for (const auto candidate : candidates) {
        const auto phase = find_type9_phase(candidate, parsed, primary);
        if (!phase) {
            continue;
        }
        AdxKeyState key{*phase, candidate.mult, candidate.add};
        const float score = type9_decode_score(sources, parsed, key);
        if (score > best.score) {
            best.key = key;
            best.score = score;
            best.canonical_type9_code = key9_canonical_code(key);
        }
    }
    if (!std::isfinite(best.score)) {
        return std::unexpected("ADX recovery: no type-9 triplet matched the observed bit stream");
    }
    add_result_candidate(best);
    return best;
}

std::expected<AdxRecoveryResult, std::string> recover_key_from_scales(
    std::span<const AdxRecoverySource> encrypted_sources,
    std::span<const std::span<const uint16_t>> plaintext_scales) {
    if (encrypted_sources.empty() || encrypted_sources.size() != plaintext_scales.size()) {
        return std::unexpected("ADX recovery: encrypted/plaintext source count mismatch");
    }
    auto encrypted_result = parse_sources(encrypted_sources, 9u);
    if (!encrypted_result) return std::unexpected(encrypted_result.error());
    auto& encrypted = *encrypted_result;
    if (encrypted.front().size() < 2u || plaintext_scales.front().size() < 2u) {
        return std::unexpected("ADX recovery: at least two known type-9 scales are required");
    }

    const uint16_t start = static_cast<uint16_t>(
        (encrypted.front()[0].scale ^ plaintext_scales.front()[0]) & 0x1FFFu);
    const uint16_t second = static_cast<uint16_t>(
        (encrypted.front()[1].scale ^ plaintext_scales.front()[1]) & 0x1FFFu);
    AdxRecoveryResult result{
        .encryption_type = 9u,
        .total_frames = total_frame_count(encrypted),
        .source_frames = source_frame_counts(encrypted),
        .candidates = {},
        .source_count = encrypted_sources.size(),
    };
    for (uint16_t mult = 1u; mult < 0x2000u; mult = static_cast<uint16_t>(mult + 4u)) {
        const uint16_t add = static_cast<uint16_t>((second - start * mult) & 0x1FFFu);
        if ((add & 1u) == 0u) {
            continue;
        }
        const AdxKeyState candidate{start, mult, add};
        bool valid = true;
        for (size_t source_index = 0; source_index < encrypted.size() && valid; ++source_index) {
            const auto& cipher = encrypted[source_index];
            const auto plain = plaintext_scales[source_index];
            if (cipher.size() > plain.size()) {
                valid = false;
                break;
            }
            AdxKeyState state = candidate;
            for (size_t frame = 0; frame < cipher.size(); ++frame) {
                const uint16_t recovered = static_cast<uint16_t>(
                    (cipher[frame].scale ^ state.xor_value) & 0x1FFFu);
                if (recovered != (plain[frame] & 0x1FFFu)) {
                    valid = false;
                    break;
                }
                state.advance();
            }
        }
        if (valid) {
            result.key = candidate;
            result.canonical_type9_code = key9_canonical_code(candidate);
            result.score = 1.0f;
            result.examined_frames = result.total_frames;
            result.evidence_frames = evidence_frame_count(encrypted);
            result.evidence_count = result.evidence_frames;
            add_result_candidate(result);
            return result;
        }
    }
    return std::unexpected("ADX recovery: no type-9 triplet matches known scales");
}

std::expected<AdxRecoveryResult, std::string> recover_key(
    std::span<const AdxRecoverySource> sources,
    KeyRecoveryMode mode)
{
    if (mode == KeyRecoveryMode::SharedBaseKey || sources.size() <= 1u) {
        return recover_key(sources);
    }
    if (sources.empty()) return std::unexpected("ADX recovery: no sources");

    AdxRecoveryResult combined;
    combined.source_count = sources.size();
    for (const auto source : sources) {
        const std::array one{source};
        auto recovered = recover_key(one);
        if (!recovered) return std::unexpected(recovered.error());
        combined.examined_frames += recovered->examined_frames;
        combined.evidence_frames += recovered->evidence_frames;
        combined.evidence_count += recovered->evidence_count;
        combined.total_frames += recovered->total_frames;
        combined.source_frames.insert(
            combined.source_frames.end(), recovered->source_frames.begin(), recovered->source_frames.end());
        for (const auto& candidate : recovered->candidates) {
            auto existing = std::ranges::find_if(combined.candidates, [&](const auto& current) {
                return current.key == candidate.key;
            });
            if (existing == combined.candidates.end()) {
                combined.candidates.push_back(candidate);
                continue;
            }
            const size_t count = existing->source_count + candidate.source_count;
            existing->score = static_cast<float>(
                (static_cast<double>(existing->score) * existing->source_count +
                 static_cast<double>(candidate.score) * candidate.source_count) / count);
            existing->source_count = count;
            existing->evidence_count += candidate.evidence_count;
            existing->evidence_frames += candidate.evidence_frames;
        }
    }
    std::ranges::stable_sort(combined.candidates, [](const auto& left, const auto& right) {
        if (left.source_count != right.source_count) return left.source_count > right.source_count;
        if (left.score != right.score) return left.score > right.score;
        return left.evidence_count > right.evidence_count;
    });
    if (combined.candidates.size() > MaxKeyRecoveryCandidates) {
        combined.candidates.resize(MaxKeyRecoveryCandidates);
    }
    if (!combined.candidates.empty()) {
        const auto& best = combined.candidates.front();
        combined.key = best.key;
        combined.score = best.score;
        combined.canonical_type9_code = best.canonical_type9_code;
    }
    return combined;
}

} // namespace cricodecs::adx
