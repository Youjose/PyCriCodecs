/**
 * @file awb_aac_key_recovery.cpp
 * @brief Deterministic recovery of CRI AWB AAC effective keys.
 */

#include "awb_aac_key_recovery.hpp"

#include "../utilities/io_endian.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace cricodecs::awb {
namespace {

constexpr uint64_t EffectiveKeyMask = (uint64_t{1} << 52u) - 1u;
constexpr size_t MinimumM4aSize = 40;
constexpr size_t MdatOffset = 32;

constexpr std::array<uint8_t, 32> CriM4aPrefix{
    0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p',
    'i', 's', 'o', 'm', 0x00, 0x00, 0x00, 0x00,
    'i', 's', 'o', 'm', 'm', 'p', '4', '2',
    0x00, 0x00, 0x00, 0x08, 'f', 'r', 'e', 'e',
};

enum class BoxType : uint32_t {
    Ftyp = io::FourCC{"ftyp"}.be_value(),
    Free = io::FourCC{"free"}.be_value(),
    Mdat = io::FourCC{"mdat"}.be_value(),
    Moov = io::FourCC{"moov"}.be_value(),
    Mvhd = io::FourCC{"mvhd"}.be_value(),
    Trak = io::FourCC{"trak"}.be_value(),
    Edts = io::FourCC{"edts"}.be_value(),
    Mdia = io::FourCC{"mdia"}.be_value(),
    Minf = io::FourCC{"minf"}.be_value(),
    Dinf = io::FourCC{"dinf"}.be_value(),
    Stbl = io::FourCC{"stbl"}.be_value(),
    Stsd = io::FourCC{"stsd"}.be_value(),
    Mp4a = io::FourCC{"mp4a"}.be_value(),
    Esds = io::FourCC{"esds"}.be_value(),
    Stts = io::FourCC{"stts"}.be_value(),
    Stsc = io::FourCC{"stsc"}.be_value(),
    Stsz = io::FourCC{"stsz"}.be_value(),
    Stco = io::FourCC{"stco"}.be_value(),
    Udta = io::FourCC{"udta"}.be_value(),
};

constexpr uint16_t inverse_odd(uint16_t value) noexcept {
    uint32_t inverse = value;
    for (int i = 0; i < 4; ++i) {
        inverse *= 2u - (static_cast<uint32_t>(value) * inverse);
    }
    return static_cast<uint16_t>(inverse);
}

struct Seeds {
    uint16_t k0 = 0;
    uint16_t k1 = 0;
    uint16_t k2 = 0;
    uint16_t k3 = 0;
};

struct ParameterState {
    uint16_t mul = 0;
    uint16_t add = 0;
    uint16_t k2 = 0;
    uint16_t k3 = 0;

    auto operator<=>(const ParameterState&) const = default;
};

uint16_t next_mul(uint16_t previous, const Seeds& seeds) noexcept {
    const uint32_t value = (4u * seeds.k1) + (seeds.k0 * (previous & 0xFFFCu));
    return static_cast<uint16_t>((value & 0xFFFDu) | 1u);
}

uint16_t next_add(uint16_t previous, const Seeds& seeds) noexcept {
    const uint32_t value = (2u * seeds.k3) + (seeds.k2 * (previous & 0xFFFEu));
    return static_cast<uint16_t>(value | 1u);
}

uint16_t advance_state(uint16_t state, uint16_t mul, uint16_t add, uint32_t count) noexcept {
    uint16_t power_mul = mul;
    uint16_t power_add = add;
    uint16_t result_mul = 1;
    uint16_t result_add = 0;
    while (count != 0) {
        if ((count & 1u) != 0) {
            result_add = static_cast<uint16_t>(
                (static_cast<uint32_t>(power_mul) * result_add) + power_add);
            result_mul = static_cast<uint16_t>(
                static_cast<uint32_t>(power_mul) * result_mul);
        }
        power_add = static_cast<uint16_t>(
            static_cast<uint32_t>(power_add) * (static_cast<uint32_t>(power_mul) + 1u));
        power_mul = static_cast<uint16_t>(
            static_cast<uint32_t>(power_mul) * power_mul);
        count >>= 1u;
    }
    return static_cast<uint16_t>((static_cast<uint32_t>(result_mul) * state) + result_add);
}

std::pair<uint16_t, uint16_t> parameters_at(uint32_t offset, const Seeds& seeds) noexcept {
    uint16_t mul = static_cast<uint16_t>((4u * seeds.k1) | 1u);
    uint16_t add = static_cast<uint16_t>((2u * seeds.k3) | 1u);
    for (uint32_t block = 0; block <= (offset >> 16u); ++block) {
        mul = next_mul(mul, seeds);
        add = next_add(add, seeds);
    }
    return {mul, add};
}

uint16_t state_at(uint32_t offset, const Seeds& seeds) noexcept {
    const auto [mul, add] = parameters_at(offset, seeds);
    const uint16_t initial = static_cast<uint16_t>((2u * seeds.k3) | 1u);
    return advance_state(initial, mul, add, (offset & 0xFFFFu) + 1u);
}

uint8_t plaintext_at(std::span<const uint8_t> bytes, uint32_t offset, const Seeds& seeds) noexcept {
    return bytes[offset] ^ static_cast<uint8_t>(state_at(offset, seeds) >> 8u);
}

uint32_t plaintext_be32(std::span<const uint8_t> bytes, uint32_t offset, const Seeds& seeds) noexcept {
    uint32_t value = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        value = (value << 8u) | plaintext_at(bytes, offset + i, seeds);
    }
    return value;
}

std::vector<uint8_t> decrypt_range(
    std::span<const uint8_t> bytes, uint32_t offset, size_t size, const Seeds& seeds) {
    const uint16_t initial = static_cast<uint16_t>((2u * seeds.k3) | 1u);
    auto [mul, add] = parameters_at(offset, seeds);
    uint16_t state = advance_state(initial, mul, add, offset & 0xFFFFu);
    std::vector<uint8_t> clear(size);
    for (size_t i = 0; i < size; ++i) {
        const uint32_t current = offset + static_cast<uint32_t>(i);
        if (i != 0 && static_cast<uint16_t>(current) == 0) {
            mul = next_mul(mul, seeds);
            add = next_add(add, seeds);
        }
        state = static_cast<uint16_t>((static_cast<uint32_t>(state) * mul) + add);
        clear[i] = bytes[current] ^ static_cast<uint8_t>(state >> 8u);
    }
    return clear;
}

bool plaintext_matches(
    std::span<const uint8_t> bytes,
    uint32_t offset,
    std::span<const uint8_t> expected,
    const Seeds& seeds) noexcept {
    for (uint32_t i = 0; i < expected.size(); ++i) {
        if (plaintext_at(bytes, offset + i, seeds) != expected[i]) return false;
    }
    return true;
}

bool prefix_matches(std::span<const uint8_t> bytes, const Seeds& seeds) noexcept {
    if (bytes.size() < MinimumM4aSize) {
        return false;
    }
    constexpr std::array<uint8_t, 4> Mdat{'m', 'd', 'a', 't'};
    return plaintext_matches(bytes, 0, CriM4aPrefix, seeds)
        && plaintext_matches(bytes, 36, Mdat, seeds);
}

bool exact_top_level_layout(std::span<const uint8_t> bytes, const Seeds& seeds) noexcept {
    if (!prefix_matches(bytes, seeds) || bytes.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    const uint32_t mdat_size = plaintext_be32(bytes, MdatOffset, seeds);
    if (mdat_size < 8u) {
        return false;
    }
    const uint64_t moov_offset = MdatOffset + static_cast<uint64_t>(mdat_size);
    if (moov_offset + 8u > bytes.size()) {
        return false;
    }

    constexpr std::array<uint8_t, 4> Moov{'m', 'o', 'o', 'v'};
    if (!plaintext_matches(bytes, static_cast<uint32_t>(moov_offset) + 4u, Moov, seeds)) {
        return false;
    }

    const uint32_t moov_size = plaintext_be32(bytes, static_cast<uint32_t>(moov_offset), seeds);
    return moov_size >= 8u && moov_offset + moov_size == bytes.size();
}

std::vector<ParameterState> recover_parameter_states(std::span<const uint8_t> bytes) {
    std::array<uint8_t, 40> high{};
    for (size_t i = 0; i < CriM4aPrefix.size(); ++i) {
        high[i] = bytes[i] ^ CriM4aPrefix[i];
    }
    constexpr std::array<uint8_t, 4> Mdat{'m', 'd', 'a', 't'};
    for (size_t i = 0; i < Mdat.size(); ++i) {
        high[36 + i] = bytes[36 + i] ^ Mdat[i];
    }

    std::vector<ParameterState> states;
    for (uint32_t mul_value = 1; mul_value < 0x10000u; mul_value += 8u) {
        const auto mul = static_cast<uint16_t>(mul_value);
        const uint16_t inverse = inverse_odd(mul);
        for (uint32_t low4 = 0; low4 < 0x100u; ++low4) {
            const uint16_t state4 = static_cast<uint16_t>((high[4] << 8u) | low4);
            const uint32_t first_low5 = ((mul_value * low4) + 1u) & 3u;
            for (uint32_t low5 = first_low5; low5 < 0x100u; low5 += 4u) {
                const uint16_t state5 = static_cast<uint16_t>((high[5] << 8u) | low5);
                const uint16_t add = static_cast<uint16_t>(
                    state5 - (static_cast<uint32_t>(mul) * state4));
                const uint16_t state6 = static_cast<uint16_t>(
                    (static_cast<uint32_t>(mul) * state5) + add);
                if (static_cast<uint8_t>(state6 >> 8u) != high[6]) {
                    continue;
                }
                const uint16_t state7 = static_cast<uint16_t>(
                    (static_cast<uint32_t>(mul) * state6) + add);
                if (static_cast<uint8_t>(state7 >> 8u) != high[7]) {
                    continue;
                }

                std::array<uint16_t, 40> stream{};
                stream[4] = state4;
                stream[5] = state5;
                for (int position = 3; position >= 0; --position) {
                    stream[static_cast<size_t>(position)] = static_cast<uint16_t>(
                        static_cast<uint32_t>(inverse) *
                        static_cast<uint16_t>(stream[static_cast<size_t>(position) + 1] - add));
                }
                for (size_t position = 6; position < stream.size(); ++position) {
                    stream[position] = static_cast<uint16_t>(
                        (static_cast<uint32_t>(mul) * stream[position - 1]) + add);
                }

                bool matches = true;
                for (size_t position = 0; position < CriM4aPrefix.size(); ++position) {
                    matches &= static_cast<uint8_t>(stream[position] >> 8u) == high[position];
                }
                for (size_t position = 36; position < 40; ++position) {
                    matches &= static_cast<uint8_t>(stream[position] >> 8u) == high[position];
                }
                if (!matches) {
                    continue;
                }

                const uint16_t initial = static_cast<uint16_t>(
                    static_cast<uint32_t>(inverse) * static_cast<uint16_t>(stream[0] - add));
                if ((initial & 3u) != 3u) {
                    continue;
                }
                const uint16_t k3 = static_cast<uint16_t>((initial - 1u) / 2u);
                if (k3 >= 0x8000u || (k3 & 1u) == 0u) {
                    continue;
                }

                for (uint32_t k2_value = 1; k2_value < 0x8000u; k2_value += 4u) {
                    const auto k2 = static_cast<uint16_t>(k2_value);
                    const Seeds seeds{0, 0, k2, k3};
                    if (next_add(initial, seeds) != add) {
                        continue;
                    }
                    const ParameterState state{mul, add, k2, k3};
                    if (std::ranges::find(states, state) == states.end()) {
                        states.push_back(state);
                    }
                }
            }
        }
    }
    return states;
}

std::vector<std::pair<uint16_t, uint16_t>> recover_mul_seeds(uint16_t wanted_mul) {
    std::vector<std::pair<uint16_t, uint16_t>> seeds;
    for (uint32_t k0_value = 1; k0_value < 0x4000u; k0_value += 4u) {
        for (uint32_t k1_value = 1; k1_value < 0x4000u; k1_value += 2u) {
            const Seeds candidate{
                static_cast<uint16_t>(k0_value),
                static_cast<uint16_t>(k1_value),
                0,
                0,
            };
            const uint16_t initial_mul = static_cast<uint16_t>((4u * candidate.k1) | 1u);
            if (next_mul(initial_mul, candidate) == wanted_mul) {
                seeds.emplace_back(candidate.k0, candidate.k1);
            }
        }
    }
    return seeds;
}

uint64_t key_from_seeds(const Seeds& seeds) noexcept {
    return ((static_cast<uint64_t>(seeds.k0 - 1u) / 4u))
        | ((static_cast<uint64_t>(seeds.k1 - 1u) / 2u) << 12u)
        | ((static_cast<uint64_t>(seeds.k2 - 1u) / 4u) << 25u)
        | ((static_cast<uint64_t>(seeds.k3 - 1u) / 2u) << 38u);
}

constexpr std::array RequiredBoxes{
    BoxType::Mvhd, BoxType::Trak, BoxType::Mdia, BoxType::Minf,
    BoxType::Stbl, BoxType::Stsd, BoxType::Mp4a, BoxType::Esds,
    BoxType::Stts, BoxType::Stsc, BoxType::Stsz, BoxType::Stco,
};

bool is_plain_container(BoxType type) noexcept {
    return type == BoxType::Moov
        || type == BoxType::Trak
        || type == BoxType::Edts
        || type == BoxType::Mdia
        || type == BoxType::Minf
        || type == BoxType::Dinf
        || type == BoxType::Stbl
        || type == BoxType::Udta;
}

bool parse_boxes(std::span<const uint8_t> bytes, size_t begin, size_t end, uint16_t& features) {
    size_t position = begin;
    while (position < end) {
        if (end - position < 8) {
            return false;
        }
        uint64_t box_size = io::read_be<uint32_t>(bytes, position);
        const auto type = static_cast<BoxType>(io::read_be<uint32_t>(bytes, position + 4));
        size_t header_size = 8;
        if (box_size == 1) {
            if (end - position < 16) {
                return false;
            }
            box_size = io::read_be<uint64_t>(bytes, position + 8);
            header_size = 16;
        } else if (box_size == 0) {
            box_size = end - position;
        }
        if (box_size < header_size || box_size > end - position) {
            return false;
        }

        if (const auto feature = std::ranges::find(RequiredBoxes, type); feature != RequiredBoxes.end()) {
            features |= uint16_t{1} << (feature - RequiredBoxes.begin());
        }
        size_t child_begin = 0;
        if (is_plain_container(type)) {
            child_begin = position + header_size;
        } else if (type == BoxType::Stsd) {
            if (box_size < header_size + 8u) {
                return false;
            }
            child_begin = position + header_size + 8u;
        } else if (type == BoxType::Mp4a) {
            if (box_size < header_size + 28u) {
                return false;
            }
            child_begin = position + header_size + 28u;
        }

        const size_t box_end = position + static_cast<size_t>(box_size);
        if (child_begin != 0 && !parse_boxes(bytes, child_begin, box_end, features)) {
            return false;
        }
        position = box_end;
    }
    return position == end;
}

struct StructuralScore {
    size_t points = 0;
    bool complete = false;
};

constexpr size_t MaximumStructuralScore = 6 + RequiredBoxes.size();

StructuralScore score_m4a(std::span<const uint8_t> bytes, const Seeds& seeds) {
    StructuralScore score;
    if (!exact_top_level_layout(bytes, seeds)) {
        return score;
    }
    score.points = 5;

    const uint32_t moov_offset = MdatOffset + plaintext_be32(bytes, MdatOffset, seeds);
    const auto moov = decrypt_range(bytes, moov_offset, bytes.size() - moov_offset, seeds);
    uint16_t features = 0;
    const bool bounded = parse_boxes(moov, 0, moov.size(), features);
    score.points += bounded ? 1u : 0u;
    score.points += static_cast<size_t>(std::popcount(features));
    score.complete = bounded && std::popcount(features) == RequiredBoxes.size();
    return score;
}

} // namespace

std::expected<KeyRecoveryResult, std::string> recover_aac_key(
    std::span<const AacRecoverySource> sources) {
    if (sources.empty()) {
        return std::unexpected("AWB AAC key recovery failed: no M4A payloads were supplied");
    }
    for (const auto& source : sources) {
        if (source.bytes.size() < MinimumM4aSize) {
            return std::unexpected("AWB AAC key recovery failed: an input is too short for the supported M4A layout");
        }
        if (source.bytes.size() > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected("AWB AAC key recovery failed: inputs larger than 4 GiB are not supported");
        }
        if (static_cast<BoxType>(io::read_be<uint32_t>(source.bytes, 4)) == BoxType::Ftyp) {
            return std::unexpected("AWB AAC key recovery failed: an input is already clear M4A data");
        }
    }

    for (size_t index = 1; index < sources.size(); ++index) {
        const auto first = sources.front().bytes;
        const auto current = sources[index].bytes;
        const bool same_prefix = std::equal(first.begin(), first.begin() + 32, current.begin());
        const bool same_mdat_tag = std::equal(first.begin() + 36, first.begin() + 40, current.begin() + 36);
        if (!same_prefix || !same_mdat_tag) {
            return std::unexpected(
                "AWB AAC key recovery failed: inputs do not share one supported encrypted M4A prefix and key");
        }
    }

    const auto parameter_states = recover_parameter_states(sources.front().bytes);
    if (parameter_states.empty()) {
        return std::unexpected(
            "AWB AAC key recovery failed: the input does not match the supported CRI M4A prefix");
    }

    struct RankedCandidate {
        uint64_t key = 0;
        size_t points = 0;
        size_t validated_sources = 0;
    };

    std::vector<RankedCandidate> ranked;
    size_t accepted_candidates = 0;
    uint16_t cached_mul = 0;
    std::vector<std::pair<uint16_t, uint16_t>> mul_seeds;
    for (const auto& state : parameter_states) {
        if (mul_seeds.empty() || cached_mul != state.mul) {
            cached_mul = state.mul;
            mul_seeds = recover_mul_seeds(state.mul);
        }
        for (const auto& [k0, k1] : mul_seeds) {
            const Seeds seeds{k0, k1, state.k2, state.k3};
            if (!std::ranges::all_of(sources, [&](const AacRecoverySource& source) {
                    return exact_top_level_layout(source.bytes, seeds);
                })) {
                continue;
            }

            ++accepted_candidates;
            RankedCandidate candidate;
            candidate.key = key_from_seeds(seeds) & EffectiveKeyMask;
            for (const auto& source : sources) {
                const auto score = score_m4a(source.bytes, seeds);
                candidate.points += score.points;
                candidate.validated_sources += score.complete ? 1u : 0u;
            }

            auto existing = std::ranges::find(ranked, candidate.key, &RankedCandidate::key);
            if (existing == ranked.end()) {
                ranked.push_back(candidate);
            } else if (candidate.points > existing->points ||
                       (candidate.points == existing->points &&
                        candidate.validated_sources > existing->validated_sources)) {
                *existing = candidate;
            }
        }
    }

    if (ranked.empty()) {
        return std::unexpected(
            "AWB AAC key recovery failed: no candidate produced a bounded trailing moov atom");
    }

    std::ranges::sort(ranked, [](const RankedCandidate& left, const RankedCandidate& right) {
        if (left.points != right.points) return left.points > right.points;
        if (left.validated_sources != right.validated_sources) {
            return left.validated_sources > right.validated_sources;
        }
        return left.key < right.key;
    });
    if (ranked.size() > MaxKeyRecoveryCandidates) {
        ranked.resize(MaxKeyRecoveryCandidates);
    }
    std::vector<KeyCandidate> candidates;
    candidates.reserve(ranked.size());
    for (const auto& candidate : ranked) {
        candidates.push_back(KeyCandidate{
            .key = candidate.key,
            .score = static_cast<float>(candidate.points) /
                static_cast<float>(MaximumStructuralScore * sources.size()),
            .validated_sources = candidate.validated_sources,
            .source_count = sources.size(),
            .candidate_count = accepted_candidates,
        });
    }
    return KeyRecoveryResult{
        .candidates = std::move(candidates),
        .source_count = sources.size(),
        .evidence_count = sources.size(),
    };
}

} // namespace cricodecs::awb
