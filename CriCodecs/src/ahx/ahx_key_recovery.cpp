#include "ahx_key_recovery.hpp"

#include "ahx_allocation_tables.hpp"
#include "ahx_format.hpp"

#include "../adx/adx_crypto.hpp"
#include "../utilities/io.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>

namespace cricodecs::ahx {

namespace {

using detail::AHX_BANDS;
using detail::AHX_BITALLOC_TABLE;
using detail::AHX_EXPECTED_FRAME_SIZE;
using detail::AHX_FOOTER_PREFIX;
using detail::AHX_FOOTER_TAG;
using detail::AHX_GRANULES;
using detail::AHX_OFFSET_TABLE;
using detail::AHX_QBITS_TABLE;
using detail::is_ahx_frame_header;

constexpr size_t ComponentCount = 3;
constexpr size_t ConfidenceFrames = 5;

struct ParsedFrame {
    std::span<const uint8_t> bytes;
    std::array<uint8_t, AHX_BANDS> allocations{};
    std::array<uint8_t, AHX_BANDS> encrypted_scfsi{};
    uint8_t component{};
    size_t selectors_end{};
    size_t quant_bits{};
};

using ParsedSource = std::vector<ParsedFrame>;

struct CandidateRank {
    uint64_t grammar_frames{};
    uint64_t exact_frames{};

    [[nodiscard]] friend constexpr auto operator<=>(
        const CandidateRank&, const CandidateRank&) = default;
};

struct RecoveredComponent {
    uint16_t value{};
    uint32_t tied_candidates{};
    CandidateRank rank{};
    uint64_t evidence_frames{};
};

[[nodiscard]] bool can_read(const io::bit_reader& reader, size_t bits) noexcept {
    return reader.remaining() >= bits;
}

[[nodiscard]] std::expected<ParsedFrame, std::string> parse_frame(
    std::span<const uint8_t> bytes) {
    if (bytes.size() < 4u ||
        !is_ahx_frame_header(io::read_be<uint32_t>(bytes.data()))) {
        return std::unexpected("AHX recovery: invalid frame header");
    }

    ParsedFrame frame{.bytes = bytes};
    io::bit_reader reader(bytes);
    reader.skip(32);

    for (size_t band = 0; band < AHX_BANDS; ++band) {
        const int bits = AHX_BITALLOC_TABLE[band];
        if (!can_read(reader, static_cast<size_t>(bits))) {
            return std::unexpected("AHX recovery: truncated bit allocation");
        }
        frame.allocations[band] = static_cast<uint8_t>(reader.read(bits));
    }

    if (frame.allocations[0] != 0u) {
        if (!can_read(reader, 2u)) {
            return std::unexpected("AHX recovery: truncated selector");
        }
        frame.component = static_cast<uint8_t>(reader.read(2));
        frame.encrypted_scfsi[0] = frame.component;
    }

    for (size_t band = 1; band < AHX_BANDS; ++band) {
        if (frame.allocations[band] == 0u) {
            continue;
        }
        if (!can_read(reader, 2u)) {
            return std::unexpected("AHX recovery: truncated selectors");
        }
        frame.encrypted_scfsi[band] = static_cast<uint8_t>(reader.read(2));
    }
    frame.selectors_end = reader.position();

    for (size_t granule = 0; granule < AHX_GRANULES; ++granule) {
        for (size_t band = 0; band < AHX_BANDS; ++band) {
            const uint8_t allocation = frame.allocations[band];
            if (allocation == 0u) {
                continue;
            }
            const int allocation_bits = AHX_BITALLOC_TABLE[band];
            const int quant_index = AHX_OFFSET_TABLE[allocation_bits][allocation - 1u];
            const int quant = AHX_QBITS_TABLE[quant_index];
            frame.quant_bits += static_cast<size_t>(quant < 0 ? -quant : quant * 3);
        }
    }
    return frame;
}

[[nodiscard]] std::expected<ParsedSource, std::string> parse_source(
    AhxRecoverySource source, uint8_t expected_type) {
    if (source.bytes.size() < 0x24u ||
        io::read_be<uint16_t>(source.bytes.data()) != 0x8000u) {
        return std::unexpected("AHX recovery: invalid AHX header");
    }
    if (source.bytes[4] != 0x10u && source.bytes[4] != 0x11u) {
        return std::unexpected("AHX recovery: input is not AHX");
    }
    if (source.bytes[5] != 0u || source.bytes[6] != 0u ||
        source.bytes[7] != 1u || source.bytes[18] != 0x06u) {
        return std::unexpected("AHX recovery: unsupported AHX header");
    }
    if (source.bytes[19] != expected_type) {
        return std::unexpected("AHX recovery: mixed encryption types");
    }

    const size_t start_offset = static_cast<size_t>(
        io::read_be<uint16_t>(source.bytes.data() + 2u)) + 4u;
    if (start_offset + 4u > source.bytes.size()) {
        return std::unexpected("AHX recovery: invalid audio offset");
    }

    ParsedSource parsed;
    size_t offset = start_offset;
    while (offset + 4u <= source.bytes.size()) {
        const uint32_t marker = io::read_be<uint32_t>(source.bytes.data() + offset);
        if (marker == AHX_FOOTER_PREFIX || marker == AHX_FOOTER_TAG) {
            break;
        }
        if (!is_ahx_frame_header(marker)) {
            return std::unexpected("AHX recovery: invalid frame boundary");
        }

        size_t next = 0;
        const size_t scan_end = std::min(
            source.bytes.size(), offset + AHX_EXPECTED_FRAME_SIZE + 4u);
        for (size_t position = offset + 4u; position + 4u <= scan_end; ++position) {
            const uint32_t next_marker = io::read_be<uint32_t>(source.bytes.data() + position);
            if (is_ahx_frame_header(next_marker) ||
                next_marker == AHX_FOOTER_PREFIX || next_marker == AHX_FOOTER_TAG) {
                next = position;
                break;
            }
        }
        if (next == 0u && source.bytes.size() - offset <= AHX_EXPECTED_FRAME_SIZE) {
            next = source.bytes.size();
        }
        if (next <= offset || next - offset > AHX_EXPECTED_FRAME_SIZE) {
            return std::unexpected("AHX recovery: failed to locate next frame");
        }

        auto frame = parse_frame(source.bytes.subspan(offset, next - offset));
        if (!frame) {
            return std::unexpected(frame.error());
        }
        parsed.push_back(std::move(*frame));
        offset = next;
    }
    if (parsed.empty()) {
        return std::unexpected("AHX recovery: no audio frames");
    }
    return parsed;
}

[[nodiscard]] size_t quant_start(const ParsedFrame& frame, uint16_t key) noexcept {
    size_t position = frame.selectors_end;
    for (size_t band = 0; band < AHX_BANDS; ++band) {
        if (frame.allocations[band] == 0u) {
            continue;
        }

        uint8_t selector = frame.encrypted_scfsi[band];
        if (band > 0u && band <= 8u) {
            selector ^= static_cast<uint8_t>((key >> (2u * (band - 1u))) & 0x03u);
        }
        constexpr std::array<uint8_t, 4> ScaleBits{18u, 12u, 6u, 12u};
        position += ScaleBits[selector];
    }
    return position;
}

[[nodiscard]] bool exact_frame_size(const ParsedFrame& frame, size_t start) noexcept {
    return (start + frame.quant_bits + 7u) / 8u == frame.bytes.size();
}

[[nodiscard]] bool grouped_codes_valid(const ParsedFrame& frame, size_t start) noexcept {
    io::bit_reader reader(frame.bytes);
    if (start > frame.bytes.size() * 8u) {
        return false;
    }
    reader.set_position(start);

    for (size_t granule = 0; granule < AHX_GRANULES; ++granule) {
        for (size_t band = 0; band < AHX_BANDS; ++band) {
            const uint8_t allocation = frame.allocations[band];
            if (allocation == 0u) {
                continue;
            }
            const int allocation_bits = AHX_BITALLOC_TABLE[band];
            const int quant_index = AHX_OFFSET_TABLE[allocation_bits][allocation - 1u];
            const int quant = AHX_QBITS_TABLE[quant_index];
            const int bits = quant < 0 ? -quant : quant * 3;
            if (!can_read(reader, static_cast<size_t>(bits))) {
                return false;
            }
            if (quant < 0) {
                const uint32_t code = reader.read(bits);
                const uint32_t limit = quant == -5 ? 27u : quant == -7 ? 125u : 729u;
                if (code >= limit) {
                    return false;
                }
            } else {
                reader.skip(bits);
            }
        }
    }
    return true;
}

[[nodiscard]] CandidateRank rank_candidate(
    uint16_t key, uint8_t component, std::span<const ParsedSource> sources) noexcept {
    CandidateRank rank;
    for (const auto& source : sources) {
        for (const auto& frame : source) {
            if (frame.component != component) {
                continue;
            }
            const size_t start = quant_start(frame, key);
            if (!exact_frame_size(frame, start)) {
                continue;
            }
            ++rank.exact_frames;
            if (grouped_codes_valid(frame, start)) {
                ++rank.grammar_frames;
            }
        }
    }
    return rank;
}

template <typename CandidateRange>
[[nodiscard]] RecoveredComponent recover_component(
    uint8_t component, std::span<const ParsedSource> sources,
    CandidateRange&& candidates) {
    RecoveredComponent recovered;
    for (const auto& source : sources) {
        recovered.evidence_frames += static_cast<uint64_t>(std::ranges::count_if(
            source, [component](const ParsedFrame& frame) {
                return frame.component == component;
            }));
    }

    bool have_candidate = false;
    for (const uint16_t candidate : candidates) {
        const CandidateRank rank = rank_candidate(candidate, component, sources);
        if (!have_candidate || rank > recovered.rank) {
            recovered.value = candidate;
            recovered.rank = rank;
            recovered.tied_candidates = 1u;
            have_candidate = true;
        } else if (rank == recovered.rank) {
            ++recovered.tied_candidates;
        }
    }
    return recovered;
}

[[nodiscard]] auto type9_values(uint32_t count, uint32_t scale, uint32_t add) {
    return std::views::iota(0u, count) |
        std::views::transform([=](uint32_t value) {
            return static_cast<uint16_t>(value * scale + add);
        });
}

[[nodiscard]] uint64_t canonical_type9_code(AhxKey key) noexcept {
    const uint64_t packed =
        (static_cast<uint64_t>(key.start & 0x7FFFu) << 27u) |
        (static_cast<uint64_t>(key.mult & 0x7FFCu) << 12u) |
        (static_cast<uint64_t>(key.add & 0x7FFFu) >> 1u);
    return packed + 1u;
}

[[nodiscard]] float component_score(const RecoveredComponent& component) noexcept {
    if (component.evidence_frames == 0u || component.tied_candidates == 0u) {
        return 0.0f;
    }
    const float agreement = static_cast<float>(component.rank.grammar_frames) /
        static_cast<float>(component.evidence_frames);
    const float evidence = static_cast<float>(std::min<uint64_t>(
        component.evidence_frames, ConfidenceFrames)) /
        static_cast<float>(ConfidenceFrames);
    return agreement * evidence / static_cast<float>(component.tied_candidates);
}

} // namespace

std::expected<AhxRecoveryResult, std::string> recover_key(
    std::span<const AhxRecoverySource> sources) {
    if (sources.empty()) {
        return std::unexpected("AHX recovery: no sources");
    }
    if (sources.front().bytes.size() <= 19u) {
        return std::unexpected("AHX recovery: truncated AHX header");
    }
    const uint8_t encryption_type = sources.front().bytes[19];
    if (encryption_type != 8u && encryption_type != 9u) {
        return std::unexpected("AHX recovery: input is not encrypted with type 8 or 9");
    }

    std::vector<ParsedSource> parsed;
    parsed.reserve(sources.size());
    for (const AhxRecoverySource source : sources) {
        auto parsed_source = parse_source(source, encryption_type);
        if (!parsed_source) {
            return std::unexpected(parsed_source.error());
        }
        parsed.push_back(std::move(*parsed_source));
    }

    std::array<RecoveredComponent, ComponentCount> components{};
    if (encryption_type == 8u) {
        for (size_t index = 0; index < components.size(); ++index) {
            components[index] = recover_component(
                static_cast<uint8_t>(index + 1u), parsed, adx::KEY8_PRIMES);
        }
    } else {
        components[0] = recover_component(1u, parsed, type9_values(0x8000, 1, 0));
        components[1] = recover_component(2u, parsed, type9_values(0x2000, 4, 1));
        components[2] = recover_component(3u, parsed, type9_values(0x4000, 2, 1));
    }

    AhxRecoveryResult result;
    result.encryption_type = encryption_type;
    result.key = {
        .start = components[0].value,
        .mult = components[1].value,
        .add = components[2].value,
    };
    result.score = std::numeric_limits<float>::max();
    for (size_t index = 0; index < components.size(); ++index) {
        result.component_frames[index] = components[index].evidence_frames;
        result.candidate_counts[index] = components[index].tied_candidates;
        result.evidence_frames += components[index].evidence_frames;
        result.score = std::min(result.score, component_score(components[index]));
    }
    for (const auto& source : parsed) {
        result.source_frames.push_back(source.size());
        result.total_frames += source.size();
    }
    if (encryption_type == 9u) {
        result.canonical_type9_code = canonical_type9_code(result.key);
    }
    result.candidates.push_back(AhxKeyCandidate{
        .key = result.key,
        .score = result.score,
        .source_count = sources.size(),
        .evidence_count = result.evidence_frames,
        .evidence_frames = result.evidence_frames,
        .candidate_counts = result.candidate_counts,
        .canonical_type9_code = result.canonical_type9_code,
    });
    result.source_count = sources.size();
    result.evidence_count = result.evidence_frames;
    return result;
}

std::expected<AhxRecoveryResult, std::string> recover_key(
    std::span<const AhxRecoverySource> sources,
    KeyRecoveryMode mode)
{
    if (mode == KeyRecoveryMode::SharedBaseKey || sources.size() <= 1u) {
        return recover_key(sources);
    }
    if (sources.empty()) return std::unexpected("AHX recovery: no sources");

    AhxRecoveryResult combined;
    combined.source_count = sources.size();
    for (const auto source : sources) {
        const std::array one{source};
        auto recovered = recover_key(one);
        if (!recovered) return std::unexpected(recovered.error());
        combined.evidence_frames += recovered->evidence_frames;
        combined.evidence_count += recovered->evidence_count;
        combined.total_frames += recovered->total_frames;
        combined.source_frames.insert(
            combined.source_frames.end(), recovered->source_frames.begin(), recovered->source_frames.end());
        for (const auto& candidate : recovered->candidates) {
            auto existing = std::ranges::find_if(combined.candidates, [&](const auto& current) {
                return current.key.start == candidate.key.start &&
                    current.key.mult == candidate.key.mult && current.key.add == candidate.key.add;
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
        combined.candidate_counts = best.candidate_counts;
        combined.canonical_type9_code = best.canonical_type9_code;
    }
    return combined;
}

} // namespace cricodecs::ahx
