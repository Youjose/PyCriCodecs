/**
 * @file hca_key_recovery.cpp
 * @brief Ciphertext-only recovery of the effective HCA type-56 key.
 */

#include "hca_key_recovery.hpp"

#include "hca_codec.hpp"
#include "hca_crypto.hpp"
#include "hca_frame.hpp"
#include "hca_packing.hpp"
#include "hca_tables.hpp"

#include "../utilities/io.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace cricodecs::hca {

struct HcaRecoveryAccess {
    [[nodiscard]] static std::span<const uint8_t> source_bytes(const Hca& hca) noexcept {
        return hca.m_source.bytes;
    }
};

namespace {

constexpr uint64_t Mask56 = (uint64_t{1} << 56) - 1;
constexpr size_t PrefixFrameLimit = 4096;
constexpr size_t BalancedFrameLimit = 4096;
constexpr size_t HybridFrameLimit = 256;
constexpr size_t ValidationFrameLimit = 64;
constexpr double NegativeInfinity = -std::numeric_limits<double>::infinity();
using BitReader = io::bit_reader;

template <typename Worker>
void run_workers(size_t count, Worker& worker) {
    if (count == 1) {
        worker();
        return;
    }
    std::vector<std::jthread> workers;
    workers.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        workers.emplace_back(worker);
    }
}

enum class AnchorKind : uint8_t {
    Absolute,
    Prefix,
    Normalized,
    Suffix,
};

struct Anchor {
    AnchorKind kind{};
    uint16_t value{};

    friend constexpr bool operator==(const Anchor&, const Anchor&) = default;
};

struct ModelFeature {
    Anchor anchor;
    float weight;
    std::array<uint16_t, 256> counts;
};

struct Model {
    uint32_t training_frames;
    std::span<const ModelFeature> features;
};

#include "hca_key_recovery_models.inc"

struct PreparedFeature {
    Anchor anchor;
    double weight{};
    std::array<double, 256> values{};
    std::array<double, 16> high{};
};

using PreparedModel = std::vector<PreparedFeature>;

struct Payload {
    std::span<const uint8_t> bytes;
    const HcaHeader* header{};
};

struct Profile {
    uint8_t version_family{};
    uint16_t frame_size{};
    uint8_t channel_count{};
    uint8_t min_resolution{};
    uint8_t max_resolution{};
    uint8_t total_band_count{};
    uint8_t base_band_count{};
    uint8_t stereo_band_count{};
    uint8_t bands_per_hfr_group{};
    uint8_t hfr_group_count{};
    std::vector<ChannelType> channel_types;
    bool ath{};

    friend bool operator==(const Profile&, const Profile&) = default;
};

struct Layout {
    double score{};
    uint8_t q0{};
    uint8_t zero_location{};
    uint8_t ff_location{};
};

struct Scores {
    std::vector<double> values = std::vector<double>(256 * 256);
    std::array<std::array<double, 16>, 256> high{};
};

struct Metrics {
    uint32_t tested{};
    uint32_t valid{};
    uint32_t plausible{};
    uint32_t canonical_noise{};
    uint32_t canonical_scalefactors{};
    uint32_t canonical{};
    uint32_t zero_tail{};
    uint32_t wrapped_deltas{};
    uint64_t tail_one_bits{};
    uint64_t remaining_bits{};
};

struct Candidate {
    uint64_t q{};
    Metrics metrics;
    double likelihood{};
};

[[nodiscard]] constexpr auto make_rows() {
    std::array<std::array<uint8_t, 16>, 256> rows{};
    for (size_t seed = 0; seed < rows.size(); ++seed) {
        rows[seed] = cipher::type56_nibble_row(static_cast<uint8_t>(seed));
    }
    return rows;
}

inline constexpr auto Rows = make_rows();

[[nodiscard]] Profile profile(const HcaHeader& header) noexcept {
    Profile result{
        .version_family = header.file.version > HCA_VERSION_V200 ? uint8_t{3} : uint8_t{2},
        .frame_size = header.codec.frame_size,
        .channel_count = static_cast<uint8_t>(header.fmt.channel_count),
        .min_resolution = header.codec.min_resolution,
        .max_resolution = header.codec.max_resolution,
        .total_band_count = header.codec.total_band_count,
        .base_band_count = header.codec.base_band_count,
        .stereo_band_count = header.codec.stereo_band_count,
        .bands_per_hfr_group = header.codec.hfr_group_count == 0
            ? uint8_t{0}
            : header.codec.bands_per_hfr_group,
        .hfr_group_count = header.codec.hfr_group_count,
        .channel_types = std::vector<ChannelType>(header.fmt.channel_count),
        .ath = header.ath.uses_curve(),
    };
    for (size_t channel = 0; channel < result.channel_types.size(); ++channel) {
        result.channel_types[channel] = detail::channel_type(header, channel);
    }
    return result;
}

[[nodiscard]] size_t anchor_position(Anchor anchor, size_t frame_size) noexcept {
    switch (anchor.kind) {
        case AnchorKind::Absolute:
        case AnchorKind::Prefix:
            return anchor.value;
        case AnchorKind::Suffix:
            return frame_size - anchor.value;
        case AnchorKind::Normalized:
            return 2 + (static_cast<size_t>(anchor.value) * (frame_size - 5) + 24) / 49;
    }
    return 0;
}

[[nodiscard]] PreparedModel prepare(const Model& model) {
    PreparedModel result;
    result.reserve(model.features.size());
    const double denominator = static_cast<double>(model.training_frames) + 128.0;
    for (const auto& source : model.features) {
        PreparedFeature feature{.anchor = source.anchor, .weight = source.weight};
        for (size_t value = 0; value < 256; ++value) {
            feature.values[value] = std::log(
                (static_cast<double>(source.counts[value]) + 0.5) / denominator);
        }
        for (size_t high = 0; high < 16; ++high) {
            const auto begin = source.counts.begin() + static_cast<ptrdiff_t>(16 * high);
            const uint32_t count = std::accumulate(begin, begin + 16, uint32_t{0});
            feature.high[high] = std::log((static_cast<double>(count) + 8.0) / denominator);
        }
        result.push_back(std::move(feature));
    }
    return result;
}

[[nodiscard]] const PreparedFeature* find_feature(
    const PreparedModel& model, Anchor anchor) noexcept {
    const auto found = std::ranges::find(model, anchor, &PreparedFeature::anchor);
    return found == model.end() ? nullptr : &*found;
}

[[nodiscard]] PreparedModel blend(
    const PreparedModel& global, const PreparedModel& expert) {
    std::vector<Anchor> anchors;
    anchors.reserve(global.size() + expert.size());
    for (const auto& feature : global) {
        anchors.push_back(feature.anchor);
    }
    for (const auto& feature : expert) {
        if (std::ranges::find(anchors, feature.anchor) == anchors.end()) {
            anchors.push_back(feature.anchor);
        }
    }

    PreparedModel result;
    result.reserve(anchors.size());
    double weight_sum = 0.0;
    for (Anchor anchor : anchors) {
        const auto* left = find_feature(global, anchor);
        const auto* right = find_feature(expert, anchor);
        const double global_weight = left == nullptr ? 0.0 : 0.3 * left->weight;
        const double expert_weight = right == nullptr ? 0.0 : 0.7 * right->weight;
        const double total = global_weight + expert_weight;
        if (total == 0.0) {
            continue;
        }
        PreparedFeature feature{.anchor = anchor, .weight = total};
        for (size_t value = 0; value < 256; ++value) {
            const double global_value = left == nullptr ? 0.0 : left->values[value];
            const double expert_value = right == nullptr ? 0.0 : right->values[value];
            feature.values[value] =
                (global_weight * global_value + expert_weight * expert_value) / total;
        }
        for (size_t high = 0; high < 16; ++high) {
            const double global_value = left == nullptr ? 0.0 : left->high[high];
            const double expert_value = right == nullptr ? 0.0 : right->high[high];
            feature.high[high] =
                (global_weight * global_value + expert_weight * expert_value) / total;
        }
        weight_sum += total;
        result.push_back(std::move(feature));
    }
    const double mean = weight_sum / static_cast<double>(result.size());
    for (auto& feature : result) {
        feature.weight /= mean;
    }
    return result;
}

[[nodiscard]] std::vector<Payload> sample_frames(
    std::span<const Payload> payloads, size_t maximum) {
    std::vector<Payload> result;
    if (payloads.empty() || maximum == 0) {
        return result;
    }
    const size_t active = std::min(payloads.size(), maximum);
    const size_t base = maximum / active;
    const size_t extra = maximum % active;
    result.reserve(maximum);
    for (size_t payload_index = 0; payload_index < active; ++payload_index) {
        const auto& payload = payloads[payload_index];
        const auto& header = *payload.header;
        const size_t available_frames = header.available_frame_count(payload.bytes.size());
        const size_t quota = std::min<size_t>(
            available_frames, base + (payload_index < extra ? 1 : 0));
        if (quota == 0) {
            continue;
        }
        for (size_t sample = 0; sample < quota; ++sample) {
            const size_t frame_index = quota == 1
                ? 0
                : (sample * (available_frames - 1) + (quota - 1) / 2) / (quota - 1);
            const size_t offset = header.file.header_size + frame_index * header.codec.frame_size;
            result.push_back({
                .bytes = payload.bytes.subspan(offset, header.codec.frame_size),
                .header = &header,
            });
        }
    }
    return result;
}

[[nodiscard]] Scores score_model(
    const PreparedModel& model, std::span<const Payload> frames) {
    Scores scores;
    for (const auto& feature : model) {
        std::array<uint32_t, 256> counts{};
        for (const auto& frame : frames) {
            const size_t position = anchor_position(feature.anchor, frame.bytes.size());
            if (position >= 2 && position < frame.bytes.size() - 2) {
                ++counts[frame.bytes[position]];
            }
        }
        for (size_t cipher_value = 0; cipher_value < 256; ++cipher_value) {
            if (counts[cipher_value] == 0) {
                continue;
            }
            const double scale = feature.weight * counts[cipher_value];
            auto* values = scores.values.data() + cipher_value * 256;
            for (size_t plain = 0; plain < 256; ++plain) {
                values[plain] += scale * feature.values[plain];
            }
            for (size_t high = 0; high < 16; ++high) {
                scores.high[cipher_value][high] += scale * feature.high[high];
            }
        }
    }
    return scores;
}

[[nodiscard]] std::array<uint8_t, 256> backward_shuffler(
    uint8_t zero_location, uint8_t ff_location) noexcept {
    std::array<uint8_t, 256> result{};
    uint32_t location = 0;
    size_t output = 1;
    for (size_t index = 0; index < 256; ++index) {
        location = (location + 17) & 0xFF;
        if (location != zero_location && location != ff_location) {
            result[output++] = static_cast<uint8_t>(location);
        }
    }
    return result;
}

[[nodiscard]] std::vector<Layout> prefix_layouts(std::span<const Payload> frames) {
    std::array<bool, 256> byte2{};
    std::array<bool, 256> byte4{};
    for (const auto& frame : frames) {
        byte2[frame.bytes[2]] = true;
        byte4[frame.bytes[4]] = true;
    }

    std::vector<Layout> layouts;
    for (size_t q0 = 0; q0 < 256; ++q0) {
        const auto& upper = Rows[q0];
        const auto zero_row = static_cast<uint8_t>(
            std::ranges::find(upper, uint8_t{0}) - upper.begin());
        const auto ff_row = static_cast<uint8_t>(
            std::ranges::find(upper, uint8_t{15}) - upper.begin());
        for (uint8_t zero_column = 0; zero_column < 16; ++zero_column) {
            const uint8_t zero_location = static_cast<uint8_t>(16 * zero_row + zero_column);
            for (uint8_t ff_column = 0; ff_column < 16; ++ff_column) {
                const uint8_t ff_location = static_cast<uint8_t>(16 * ff_row + ff_column);
                const auto raw = backward_shuffler(zero_location, ff_location);
                bool valid = true;
                for (size_t cipher_value = 0; cipher_value < 256 && valid; ++cipher_value) {
                    if (!byte2[cipher_value] && !byte4[cipher_value]) {
                        continue;
                    }
                    const uint8_t high = cipher_value == 0
                        ? 0
                        : cipher_value == 255
                            ? 15
                            : upper[raw[cipher_value] >> 4];
                    valid = (!byte2[cipher_value] || high < 8)
                        && (!byte4[cipher_value] || high < 14);
                }
                if (valid) {
                    layouts.push_back({
                        .q0 = static_cast<uint8_t>(q0),
                        .zero_location = zero_location,
                        .ff_location = ff_location,
                    });
                }
            }
        }
    }
    return layouts;
}

void rank_layouts(std::vector<Layout>& layouts, const Scores& scores) {
    for (auto& layout : layouts) {
        const auto& upper = Rows[layout.q0];
        const auto raw = backward_shuffler(layout.zero_location, layout.ff_location);
        for (size_t cipher_value = 1; cipher_value < 255; ++cipher_value) {
            layout.score += scores.high[cipher_value][upper[raw[cipher_value] >> 4]];
        }
    }
    std::ranges::sort(layouts, [](const Layout& left, const Layout& right) {
        return std::tie(left.score, left.q0, left.zero_location, left.ff_location)
            > std::tie(right.score, right.q0, right.zero_location, right.ff_location);
    });
}

using RowLikelihoods = std::array<std::array<double, 256>, 16>;

struct CycleProblem {
    std::array<const std::array<double, 256>*, 6> unary;
    std::array<std::array<double, 256>, 6> edges{};
    std::array<std::vector<uint8_t>, 6> unary_support;
    std::array<std::vector<uint8_t>, 6> edge_support;
};

[[nodiscard]] RowLikelihoods row_likelihoods(const Scores& scores, const Layout& layout) {
    RowLikelihoods result{};
    const auto& upper = Rows[layout.q0];
    const auto raw = backward_shuffler(layout.zero_location, layout.ff_location);
    std::array<std::vector<uint8_t>, 16> cipher_by_row;
    for (size_t cipher_value = 1; cipher_value < 255; ++cipher_value) {
        cipher_by_row[raw[cipher_value] >> 4].push_back(static_cast<uint8_t>(cipher_value));
    }
    for (size_t raw_row = 0; raw_row < 16; ++raw_row) {
        const size_t high = 16 * upper[raw_row];
        for (size_t seed = 0; seed < 256; ++seed) {
            double score = 0.0;
            for (uint8_t cipher_value : cipher_by_row[raw_row]) {
                score += scores.values[
                    static_cast<size_t>(cipher_value) * 256 + high
                    + Rows[seed][raw[cipher_value] & 0x0F]];
            }
            result[raw_row][seed] = score;
        }
    }
    const size_t zero_row = layout.zero_location >> 4;
    const size_t ff_row = layout.ff_location >> 4;
    const size_t zero_column = layout.zero_location & 0x0F;
    const size_t ff_column = layout.ff_location & 0x0F;
    for (size_t seed = 0; seed < 256; ++seed) {
        if (Rows[seed][zero_column] != 0) {
            result[zero_row][seed] = NegativeInfinity;
        }
        if (Rows[seed][ff_column] != 15) {
            result[ff_row][seed] = NegativeInfinity;
        }
    }
    return result;
}

[[nodiscard]] std::vector<uint8_t> support(const std::array<double, 256>& values) {
    std::vector<uint8_t> result;
    for (size_t value = 0; value < 256; ++value) {
        if (values[value] != NegativeInfinity) {
            result.push_back(static_cast<uint8_t>(value));
        }
    }
    return result;
}

[[nodiscard]] CycleProblem make_cycle_problem(const RowLikelihoods& likelihoods) {
    CycleProblem result{
        .unary = {
            &likelihoods[0], &likelihoods[3], &likelihoods[6],
            &likelihoods[9], &likelihoods[12], &likelihoods[15],
        },
        .edges = {},
        .unary_support = {},
        .edge_support = {},
    };
    result.edges[0] = likelihoods[4];
    result.edges[4] = likelihoods[11];
    for (size_t difference = 0; difference < 256; ++difference) {
        result.edges[1][difference] = likelihoods[2][difference] + likelihoods[7][difference];
        result.edges[2][difference] = likelihoods[5][difference] + likelihoods[10][difference];
        result.edges[3][difference] = likelihoods[8][difference] + likelihoods[13][difference];
        result.edges[5][difference] = likelihoods[1][difference] + likelihoods[14][difference];
    }
    for (size_t index = 0; index < 6; ++index) {
        result.unary_support[index] = support(*result.unary[index]);
        result.edge_support[index] = support(result.edges[index]);
    }
    return result;
}

// q1 and q6 are independently grammar-refined after model recovery. Solve the
// sparse q2-q5 path first, then choose the best boundary pair for that path.
// Validation decides whether this inexpensive statistical shortcut is usable;
// the complete cycle solver remains the fallback.
[[nodiscard]] std::optional<std::array<uint8_t, 6>> solve_sparse_path(
    const CycleProblem& problem) {
    std::array<double, 256> q5_scores;
    q5_scores.fill(NegativeInfinity);
    std::array<uint8_t, 256> best_q5{};
    for (uint8_t q4 : problem.unary_support[3]) {
        for (uint8_t q5 : problem.unary_support[4]) {
            const double score = (*problem.unary[4])[q5] + problem.edges[3][q4 ^ q5];
            if (score > q5_scores[q4]) {
                q5_scores[q4] = score;
                best_q5[q4] = q5;
            }
        }
    }

    double best_core_score = NegativeInfinity;
    std::array<uint8_t, 4> core{};
    for (uint8_t q2 : problem.unary_support[1]) {
        for (uint8_t difference23 : problem.edge_support[1]) {
            const uint8_t q3 = q2 ^ difference23;
            if ((*problem.unary[2])[q3] == NegativeInfinity) {
                continue;
            }
            const double prefix = (*problem.unary[1])[q2]
                + problem.edges[1][difference23]
                + (*problem.unary[2])[q3];
            for (uint8_t difference34 : problem.edge_support[2]) {
                const uint8_t q4 = q3 ^ difference34;
                if ((*problem.unary[3])[q4] == NegativeInfinity
                    || q5_scores[q4] == NegativeInfinity) {
                    continue;
                }
                const double score = prefix
                    + problem.edges[2][difference34]
                    + (*problem.unary[3])[q4]
                    + q5_scores[q4];
                if (score > best_core_score) {
                    best_core_score = score;
                    core = {q2, q3, q4, best_q5[q4]};
                }
            }
        }
    }
    if (best_core_score == NegativeInfinity) {
        return std::nullopt;
    }

    double best_boundary_score = NegativeInfinity;
    std::array<uint8_t, 2> boundary{};
    for (uint8_t q1 : problem.unary_support[0]) {
        const double left = (*problem.unary[0])[q1] + problem.edges[0][q1 ^ core[0]];
        for (uint8_t q6 : problem.unary_support[5]) {
            const double score = left
                + (*problem.unary[5])[q6]
                + problem.edges[4][core[3] ^ q6]
                + problem.edges[5][q6 ^ q1];
            if (score > best_boundary_score) {
                best_boundary_score = score;
                boundary = {q1, q6};
            }
        }
    }
    if (best_boundary_score == NegativeInfinity) {
        return std::nullopt;
    }
    return std::array<uint8_t, 6>{
        boundary[0], core[0], core[1], core[2], core[3], boundary[1],
    };
}

[[nodiscard]] std::optional<std::array<uint8_t, 6>> solve_cycle(
    const CycleProblem& problem) {
    const auto& unary = problem.unary;
    const auto& edges = problem.edges;
    const auto& unary_support = problem.unary_support;
    const auto& edge_support = problem.edge_support;

    const size_t start = static_cast<size_t>(std::min_element(
        unary_support.begin(), unary_support.end(),
        [](const auto& left, const auto& right) { return left.size() < right.size(); })
        - unary_support.begin());
    std::array<const std::array<double, 256>*, 6> rotated_unary{};
    std::array<const std::array<double, 256>*, 6> rotated_edges{};
    std::array<const std::vector<uint8_t>*, 6> rotated_unary_support{};
    std::array<const std::vector<uint8_t>*, 6> rotated_edge_support{};
    for (size_t index = 0; index < 6; ++index) {
        const size_t source = (start + index) % 6;
        rotated_unary[index] = unary[source];
        rotated_edges[index] = &edges[source];
        rotated_unary_support[index] = &unary_support[source];
        rotated_edge_support[index] = &edge_support[source];
    }

    double best_score = NegativeInfinity;
    std::optional<std::array<uint8_t, 6>> best;
    for (uint8_t first : *rotated_unary_support[0]) {
        std::array<double, 256> previous;
        previous.fill(NegativeInfinity);
        for (uint8_t second : *rotated_unary_support[1]) {
            previous[second] = (*rotated_unary[0])[first]
                + (*rotated_unary[1])[second]
                + (*rotated_edges[0])[first ^ second];
        }
        std::array<std::array<uint8_t, 256>, 4> back{};
        bool path_exists = true;
        for (size_t stage = 1; stage < 5; ++stage) {
            std::array<double, 256> next;
            next.fill(NegativeInfinity);
            std::vector<uint8_t> active_previous;
            for (size_t value = 0; value < 256; ++value) {
                if (previous[value] != NegativeInfinity) {
                    active_previous.push_back(static_cast<uint8_t>(value));
                }
            }
            if (active_previous.empty()) {
                path_exists = false;
                break;
            }
            const auto& active_next = *rotated_unary_support[stage + 1];
            const auto& differences = *rotated_edge_support[stage];
            if (active_previous.size() * differences.size()
                < active_previous.size() * active_next.size()) {
                std::array<bool, 256> allowed{};
                for (uint8_t value : active_next) {
                    allowed[value] = true;
                }
                for (uint8_t old : active_previous) {
                    for (uint8_t difference : differences) {
                        const uint8_t value = old ^ difference;
                        if (!allowed[value]) {
                            continue;
                        }
                        const double score = previous[old] + (*rotated_edges[stage])[difference];
                        if (score > next[value]) {
                            next[value] = score;
                            back[stage - 1][value] = old;
                        }
                    }
                }
            } else {
                for (uint8_t value : active_next) {
                    for (uint8_t old : active_previous) {
                        const double score = previous[old] + (*rotated_edges[stage])[old ^ value];
                        if (score > next[value]) {
                            next[value] = score;
                            back[stage - 1][value] = old;
                        }
                    }
                }
            }
            for (size_t value = 0; value < 256; ++value) {
                previous[value] = next[value] + (*rotated_unary[stage + 1])[value];
            }
        }
        if (!path_exists) {
            continue;
        }
        for (uint8_t last : *rotated_unary_support[5]) {
            if (previous[last] == NegativeInfinity
                || (*rotated_edges[5])[last ^ first] == NegativeInfinity) {
                continue;
            }
            const double score = previous[last] + (*rotated_edges[5])[last ^ first];
            if (score <= best_score) {
                continue;
            }
            std::array<uint8_t, 6> rotated_values{first, 0, 0, 0, 0, last};
            uint8_t current = last;
            for (int stage = 3; stage >= 0; --stage) {
                current = back[static_cast<size_t>(stage)][current];
                rotated_values[static_cast<size_t>(stage) + 1] = current;
            }
            std::array<uint8_t, 6> values{};
            for (size_t index = 0; index < 6; ++index) {
                values[(start + index) % 6] = rotated_values[index];
            }
            best_score = score;
            best = values;
        }
    }
    return best;
}

[[nodiscard]] uint64_t effective_key(uint64_t q) noexcept {
    return (q + 1) & Mask56;
}

[[nodiscard]] std::array<uint8_t, 256> table_for_q(uint64_t q) noexcept {
    std::array<uint8_t, 256> table{};
    uint64_t key = effective_key(q);
    if (key == 0) {
        key = uint64_t{1} << 56;
    }
    cipher::init_cipher_type56(table, key);
    return table;
}

[[nodiscard]] double model_likelihood(const Scores& scores, uint64_t q) noexcept {
    const auto table = table_for_q(q);
    double result = 0.0;
    for (size_t cipher_value = 0; cipher_value < table.size(); ++cipher_value) {
        result += scores.values[cipher_value * 256 + table[cipher_value]];
    }
    return result;
}

[[nodiscard]] bool skip_intensity(
    BitReader& reader, ChannelType type, uint8_t hfr_groups, uint16_t version) noexcept {
    if (type != ChannelType::StereoSecondary) {
        if (version <= HCA_VERSION_V200) {
            for (size_t index = 0; index < hfr_groups; ++index) {
                static_cast<void>(reader.read(6));
            }
        }
        return reader.valid();
    }
    std::array<uint8_t, HCA_SUBFRAMES> intensity{};
    return packing::read_intensity(reader, version, intensity);
}

struct FrameMetrics {
    bool valid{};
    bool canonical_noise{};
    bool canonical_scalefactors{};
    bool zero_tail{};
    uint32_t wrapped_deltas{};
    uint32_t tail_one_bits{};
    uint32_t remaining_bits{};
};

[[nodiscard]] FrameMetrics parse_frame(
    const Payload& frame, const std::array<uint8_t, 256>& table) noexcept {
    BitReader reader(frame.bytes, table, 2, frame.bytes.size() - 2);
    if (reader.read(16) != 0xFFFF) {
        return {};
    }
    const uint32_t acceptable_noise = reader.read(9);
    const uint32_t boundary = reader.read(7);
    const int packed_noise = static_cast<int>((acceptable_noise << 8) - boundary);
    const auto& header = *frame.header;
    std::array<std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME>, HCA_MAX_CHANNELS> resolutions;
    std::array<uint8_t, HCA_MAX_CHANNELS> coded_counts;
    uint32_t wraps = 0;
    uint32_t canonical_headers = 0;

    for (size_t channel = 0; channel < header.fmt.channel_count; ++channel) {
        const ChannelType type = detail::channel_type(header, channel);
        const size_t coded_count = type == ChannelType::StereoSecondary
            ? header.codec.base_band_count
            : header.codec.base_band_count + header.codec.stereo_band_count;
        const size_t extra_count = type != ChannelType::StereoSecondary
                && header.file.version > HCA_VERSION_V200
            ? header.codec.hfr_group_count
            : 0;
        std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> scales{};
        const auto decoding = packing::read_scalefactors(
            reader, std::span(scales).first(coded_count + extra_count));
        wraps += decoding.wraps;
        canonical_headers += decoding.wraps == 0
            && decoding.delta_bits == packing::scalefactor_encoding(
                std::span(scales).first(coded_count + extra_count)).delta_bits;
        if (!skip_intensity(
                reader, type, header.codec.hfr_group_count, header.file.version)) {
            return {};
        }
        coded_counts[channel] = static_cast<uint8_t>(coded_count);
        for (size_t index = 0; index < coded_count; ++index) {
            const uint8_t scale = scales[index];
            uint8_t resolution = 0;
            if (scale > 0) {
                const int curve_position = ((packed_noise + static_cast<int>(index)) >> 8)
                    + 1 - ((5 * scale) >> 1);
                if (curve_position < 0) {
                    resolution = 15;
                } else if (curve_position <= 65) {
                    resolution = tables::RESOLUTION_INVERT_TABLE[curve_position];
                }
                resolution = std::clamp(
                    resolution, header.codec.min_resolution, header.codec.max_resolution);
            }
            resolutions[channel][index] = resolution;
        }
    }
    for (size_t subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
        for (size_t channel = 0; channel < header.fmt.channel_count; ++channel) {
            for (size_t index = 0; index < coded_counts[channel]; ++index) {
                const uint8_t resolution = resolutions[channel][index];
                if (resolution == 0) {
                    continue;
                }
                const uint8_t bits = tables::QUANTIZED_SPECTRUM_MAX_BITS[resolution];
                const uint32_t code = reader.peek(bits);
                if (resolution < 8) {
                    const uint32_t symbols = 2 * resolution + 1;
                    const uint32_t short_codes = ((1u << bits) - symbols) * 2;
                    static_cast<void>(reader.read(code < short_codes ? bits - 1 : bits));
                } else {
                    static_cast<void>(reader.read((code >> 1) == 0 ? bits - 1 : bits));
                }
            }
        }
    }
    const size_t payload_end = frame.bytes.size() * 8 - 16;
    if (!reader.valid() || reader.position() > payload_end) {
        return {};
    }
    uint32_t tail_ones = 0;
    for (size_t bit = reader.position(); bit < payload_end; ++bit) {
        tail_ones += (reader.byte(bit >> 3) >> (7 - (bit & 7))) & 1u;
    }
    return {
        .valid = true,
        .canonical_noise = acceptable_noise <= 255,
        .canonical_scalefactors = canonical_headers == header.fmt.channel_count,
        .zero_tail = tail_ones == 0,
        .wrapped_deltas = wraps,
        .tail_one_bits = tail_ones,
        .remaining_bits = static_cast<uint32_t>(payload_end - reader.position()),
    };
}

[[nodiscard]] Metrics score_q(uint64_t q, std::span<const Payload> frames) noexcept {
    Metrics result{.tested = static_cast<uint32_t>(frames.size())};
    const auto table = table_for_q(q);
    for (const auto& frame : frames) {
        const auto metrics = parse_frame(frame, table);
        if (!metrics.valid) {
            continue;
        }
        ++result.valid;
        result.plausible += metrics.wrapped_deltas == 0;
        result.canonical_noise += metrics.canonical_noise;
        result.canonical_scalefactors += metrics.canonical_scalefactors;
        result.canonical += metrics.canonical_noise && metrics.canonical_scalefactors;
        result.zero_tail += metrics.zero_tail;
        result.wrapped_deltas += metrics.wrapped_deltas;
        result.tail_one_bits += metrics.tail_one_bits;
        result.remaining_bits += metrics.remaining_bits;
    }
    return result;
}

[[nodiscard]] auto rank(const Metrics& value) noexcept {
    return std::tuple{
        value.canonical,
        value.canonical_scalefactors,
        value.canonical_noise,
        value.plausible,
        value.valid,
        -static_cast<int64_t>(value.wrapped_deltas),
        value.zero_tail,
        -static_cast<int64_t>(value.tail_one_bits),
        -static_cast<int64_t>(value.remaining_bits),
    };
}

[[nodiscard]] bool better(const Candidate& left, const Candidate& right) noexcept {
    if (rank(left.metrics) != rank(right.metrics)) {
        return rank(left.metrics) > rank(right.metrics);
    }
    if (left.likelihood != right.likelihood) {
        return left.likelihood > right.likelihood;
    }
    return left.q < right.q;
}

[[nodiscard]] std::vector<Candidate> refine_byte(
    Candidate candidate,
    std::span<const Payload> frames,
    size_t byte_index) noexcept {
    const size_t shift = 8 * byte_index;
    const uint64_t base = candidate.q & ~(uint64_t{0xFF} << shift);
    std::array<Candidate, 256> candidates{};
    for (uint64_t value = 0; value < 256; ++value) {
        candidates[value] = Candidate{
            .q = base | (value << shift),
            .metrics = {},
            .likelihood = candidate.likelihood,
        };
    }
    const std::array<size_t, 3> frame_counts{
        std::min<size_t>(2, frames.size()),
        std::min<size_t>(8, frames.size()),
        frames.size(),
    };
    const std::array<size_t, 3> keep{64, 16, MaxKeyRecoveryCandidates};
    size_t active = candidates.size();
    for (size_t stage = 0; stage < frame_counts.size(); ++stage) {
        const auto sample = frames.first(frame_counts[stage]);
        for (auto& current : std::span(candidates).first(active)) {
            current.metrics = score_q(current.q, sample);
        }
        const size_t retained = std::min(keep[stage], active);
        std::partial_sort(
            candidates.begin(), candidates.begin() + static_cast<ptrdiff_t>(retained),
            candidates.begin() + static_cast<ptrdiff_t>(active),
            [](const Candidate& left, const Candidate& right) { return better(left, right); });
        active = retained;
    }
    return {candidates.begin(), candidates.begin() + static_cast<ptrdiff_t>(active)};
}

[[nodiscard]] bool perfect(const Metrics& metrics) noexcept {
    return metrics.tested != 0
        && metrics.canonical == metrics.tested
        && metrics.valid == metrics.tested
        && metrics.zero_tail == metrics.tested;
}

[[nodiscard]] std::expected<std::vector<Candidate>, std::string> recover_with_model(
    const PreparedModel& model,
    std::span<const Payload> likelihood_frames,
    std::span<const Payload> prefix_frames,
    std::span<const Payload> validation_frames) {
    auto layouts = prefix_layouts(prefix_frames);
    if (layouts.empty()) {
        return std::unexpected("HCA key recovery failed: prefix grammar rejected every table layout");
    }
    const Scores scores = score_model(model, likelihood_frames);
    rank_layouts(layouts, scores);
    std::vector<Candidate> candidates;
    const auto append = [&](std::vector<Candidate> values) {
        for (auto& value : values) {
            if (std::ranges::none_of(candidates, [&](const Candidate& existing) {
                    return existing.q == value.q;
                })) {
                candidates.push_back(std::move(value));
            }
        }
        std::ranges::sort(candidates, [](const Candidate& left, const Candidate& right) {
            return better(left, right);
        });
        if (candidates.size() > MaxKeyRecoveryCandidates) {
            candidates.resize(MaxKeyRecoveryCandidates);
        }
    };
    for (size_t index = 0; index < std::min<size_t>(4, layouts.size()); ++index) {
        const auto likelihoods = row_likelihoods(scores, layouts[index]);
        const auto problem = make_cycle_problem(likelihoods);
        const auto sparse_tail = solve_sparse_path(problem);
        const std::array tails{
            sparse_tail,
            sparse_tail ? std::optional<std::array<uint8_t, 6>>{} : solve_cycle(problem),
        };
        for (const auto& tail : tails) {
            if (!tail) {
                continue;
            }
            uint64_t q = layouts[index].q0;
            for (size_t byte = 0; byte < tail->size(); ++byte) {
                q |= static_cast<uint64_t>((*tail)[byte]) << (8 * (byte + 1));
            }
            Candidate current{
                .q = q,
                .metrics = {},
                .likelihood = model_likelihood(scores, q),
            };
            append(refine_byte(current, validation_frames, 6));
        }
        if (sparse_tail) {
            const auto exact_tail = solve_cycle(problem);
            if (!exact_tail || *exact_tail == *sparse_tail) {
                continue;
            }
            uint64_t q = layouts[index].q0;
            for (size_t byte = 0; byte < exact_tail->size(); ++byte) {
                q |= static_cast<uint64_t>((*exact_tail)[byte]) << (8 * (byte + 1));
            }
            Candidate current{
                .q = q,
                .metrics = {},
                .likelihood = model_likelihood(scores, q),
            };
            append(refine_byte(current, validation_frames, 6));
        }
    }
    if (candidates.empty()) {
        return std::unexpected("HCA key recovery failed: no structural table candidate");
    }
    return candidates;
}

[[nodiscard]] const Model* balanced_model(const Profile& value, uint16_t version) noexcept {
    if ((version == HCA_VERSION_V102 || version == HCA_VERSION_V103
            || version == HCA_VERSION_V200 || version == HCA_VERSION_V300)
        && value.frame_size == 682
        && value.channel_count == 2
        && value.min_resolution == (version == HCA_VERSION_V300 ? 0 : 1)
        && value.max_resolution == 15
        && value.total_band_count == 128
        && value.base_band_count == 128
        && value.stereo_band_count == 0
        && value.hfr_group_count == 0
        && !value.ath) {
        return &ModelFullbandStereo48K682;
    }
    if (version == HCA_VERSION_V200
        && value.frame_size == 256
        && value.channel_count == 1
        && value.min_resolution == 1
        && value.max_resolution == 15
        && value.total_band_count == 128
        && value.base_band_count == 96
        && value.stereo_band_count == 0
        && value.bands_per_hfr_group == 4
        && value.hfr_group_count == 8
        && !value.ath) {
        return &ModelMono32KHfr256V2;
    }
    return nullptr;
}

[[nodiscard]] const Model* hybrid_model(const Profile& value) noexcept {
    const bool v3 = value.version_family == 3;
    if (value.channel_count == 1) {
        if (value.stereo_band_count != 0) {
            return nullptr;
        }
        if (value.hfr_group_count != 0) {
            return v3 ? &ModelV3MonoHfr : &ModelV2MonoHfr;
        }
        return v3 ? &ModelV3MonoFullband : &ModelV2MonoFullband;
    }

    const bool stereo = value.channel_count == 2;
    if (value.hfr_group_count != 0) {
        return v3
            ? (stereo ? &ModelV3StereoHfr : &ModelV3MultiHfr)
            : (stereo ? &ModelV2StereoHfr : &ModelV2MultiHfr);
    }
    if (value.stereo_band_count != 0) {
        return v3
            ? (stereo ? &ModelV3StereoStereo : &ModelV3MultiStereo)
            : (stereo ? &ModelV2StereoStereo : &ModelV2MultiStereo);
    }
    return v3
        ? (stereo ? &ModelV3StereoFullband : &ModelV3MultiFullband)
        : (stereo ? &ModelV2StereoFullband : &ModelV2MultiFullband);
}

[[nodiscard]] std::vector<Candidate> joint_refine(
    Candidate initial,
    std::span<const Payload> frames) {
    const uint64_t base = initial.q & ~(uint64_t{0xFF} << 8) & ~(uint64_t{0xFF} << 48);
    std::vector<Candidate> candidates;
    candidates.reserve(65536);
    for (uint64_t q1 = 0; q1 < 256; ++q1) {
        for (uint64_t q6 = 0; q6 < 256; ++q6) {
            candidates.push_back({
                .q = base | (q1 << 8) | (q6 << 48),
                .metrics = {},
                .likelihood = 0.0,
            });
        }
    }
    const std::array<size_t, 3> frame_counts{
        std::min<size_t>(2, frames.size()),
        std::min<size_t>(8, frames.size()),
        frames.size(),
    };
    const std::array<size_t, 3> keep{4096, 256, MaxKeyRecoveryCandidates};
    for (size_t stage = 0; stage < frame_counts.size(); ++stage) {
        const auto sample = frames.first(frame_counts[stage]);
        for (auto& candidate : candidates) {
            candidate.metrics = score_q(candidate.q, sample);
        }
        const auto comparator = [](const Candidate& left, const Candidate& right) {
            return better(left, right);
        };
        const size_t retained = std::min(keep[stage], candidates.size());
        std::partial_sort(
            candidates.begin(), candidates.begin() + static_cast<ptrdiff_t>(retained),
            candidates.end(), comparator);
        candidates.resize(retained);
    }
    for (auto& candidate : candidates) {
        candidate.likelihood = initial.likelihood;
    }
    return candidates;
}

[[nodiscard]] float validation_score(const Metrics& metrics) noexcept {
    if (metrics.tested == 0) {
        return 0.0f;
    }
    const uint32_t matched = std::min({metrics.canonical, metrics.valid, metrics.zero_tail});
    return static_cast<float>(matched) / static_cast<float>(metrics.tested);
}

[[nodiscard]] std::vector<KeyCandidate> public_candidates(
    std::span<const Candidate> candidates, size_t source_count) {
    std::vector<KeyCandidate> result;
    result.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        result.push_back(KeyCandidate{
            .key = effective_key(candidate.q),
            .score = validation_score(candidate.metrics),
            .source_count = source_count,
            .evidence_count = candidate.metrics.tested,
        });
    }
    return result;
}

[[nodiscard]] std::expected<std::vector<KeyCandidate>, std::string> recover_profile_key(
    std::span<const Payload> payloads,
    const Profile& common_profile,
    bool allow_joint_refine) {
    if (common_profile.ath) {
        return std::unexpected("HCA key recovery failed: ATH-profile recovery is not supported");
    }

    const auto prefix = sample_frames(payloads, PrefixFrameLimit);
    const auto validation = sample_frames(payloads, ValidationFrameLimit);
    std::vector<Candidate> candidates;
    const auto append = [&](std::span<const Candidate> values) {
        for (const auto& value : values) {
            auto existing = std::ranges::find(candidates, value.q, &Candidate::q);
            if (existing == candidates.end()) {
                candidates.push_back(value);
            } else if (better(value, *existing)) {
                *existing = value;
            }
        }
        std::ranges::sort(candidates, [](const Candidate& left, const Candidate& right) {
            return better(left, right);
        });
        if (candidates.size() > MaxKeyRecoveryCandidates) {
            candidates.resize(MaxKeyRecoveryCandidates);
        }
    };

    if (const Model* model = balanced_model(common_profile, payloads.front().header->file.version)) {
        const auto likelihood = sample_frames(payloads, BalancedFrameLimit);
        auto recovered = recover_with_model(
            prepare(*model), likelihood, prefix, validation);
        if (recovered) {
            append(*recovered);
            if (perfect(candidates.front().metrics)) {
                return public_candidates(candidates, payloads.size());
            }
        }
    }

    const Model* expert_model = hybrid_model(common_profile);
    if (expert_model == nullptr) {
        return std::unexpected("HCA key recovery failed: no model for this frame grammar");
    }
    const auto likelihood = sample_frames(payloads, HybridFrameLimit);
    const PreparedModel global = prepare(ModelGlobal);
    const PreparedModel expert = prepare(*expert_model);
    std::array<PreparedModel, 3> models{expert, blend(global, expert), global};
    for (const auto& model : models) {
        auto recovered = recover_with_model(
            model, likelihood, prefix, validation);
        if (recovered) {
            append(*recovered);
        }
    }
    if (candidates.empty()) {
        return std::unexpected("HCA key recovery failed: no table candidate could be constructed");
    }
    if (allow_joint_refine && !perfect(candidates.front().metrics)) {
        append(joint_refine(candidates.front(), validation));
    }
    return public_candidates(candidates, payloads.size());
}

} // namespace

std::expected<KeyRecoveryResult, std::string> recover_key(const Hca& source) {
    return recover_key(std::span<const Hca>(&source, 1));
}

namespace {

[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_effective_key(
    std::span<const Hca* const> sources,
    bool allow_joint_refine = true) {
    if (sources.empty()) {
        return std::unexpected("HCA key recovery failed: no HCA inputs");
    }

    std::vector<Payload> payloads;
    payloads.reserve(sources.size());
    struct ProfileGroup {
        Profile profile;
        std::vector<Payload> payloads;
    };
    std::vector<ProfileGroup> groups;
    for (const Hca* source : sources) {
        if (source == nullptr) {
            return std::unexpected("HCA key recovery failed: null HCA recovery source");
        }
        const auto bytes = HcaRecoveryAccess::source_bytes(*source);
        const auto& header = source->header();
        if (header.cipher.type != 56) {
            return std::unexpected("HCA key recovery failed: every input must use cipher type 56");
        }
        if (header.available_frame_count(bytes.size()) == 0) {
            return std::unexpected("HCA key recovery failed: input contains no complete frames");
        }
        const Profile current = profile(header);
        auto group = std::ranges::find(groups, current, &ProfileGroup::profile);
        if (group == groups.end()) {
            groups.push_back(ProfileGroup{.profile = current, .payloads = {}});
            group = std::prev(groups.end());
        }
        const Payload payload{.bytes = bytes, .header = &header};
        payloads.push_back(payload);
        group->payloads.push_back(payload);
    }

    std::vector<KeyCandidate> profile_candidates;
    std::string first_error;
    for (const auto& group : groups) {
        auto recovered = recover_profile_key(
            group.payloads, group.profile, allow_joint_refine);
        if (!recovered) {
            if (first_error.empty()) {
                first_error = recovered.error();
            }
            continue;
        }
        for (const auto& candidate : *recovered) {
            auto existing = std::ranges::find(profile_candidates, candidate.key, &KeyCandidate::key);
            if (existing == profile_candidates.end()) {
                profile_candidates.push_back(candidate);
            } else if (candidate.score > existing->score) {
                *existing = candidate;
            }
        }
    }
    if (profile_candidates.empty()) {
        return std::unexpected(first_error.empty()
            ? "HCA key recovery failed: no table candidate could be constructed"
            : std::move(first_error));
    }

    const auto validation = sample_frames(payloads, ValidationFrameLimit);
    for (auto& candidate : profile_candidates) {
        const auto metrics = score_q((candidate.key - 1u) & Mask56, validation);
        candidate.score = validation_score(metrics);
        candidate.source_count = sources.size();
        candidate.evidence_count = metrics.tested;
    }
    retain_key_candidates(profile_candidates, [](const KeyCandidate& left, const KeyCandidate& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        if (left.evidence_count != right.evidence_count) {
            return left.evidence_count > right.evidence_count;
        }
        return false;
    });
    return KeyRecoveryResult{
        .candidates = std::move(profile_candidates),
        .source_count = sources.size(),
        .evidence_count = validation.size(),
    };
}

} // namespace

std::expected<KeyRecoveryResult, std::string> recover_key(std::span<const Hca> sources) {
    std::vector<const Hca*> pointers;
    pointers.reserve(sources.size());
    for (const auto& source : sources) {
        pointers.push_back(&source);
    }
    return recover_effective_key(pointers);
}

namespace {

struct BaseClass {
    uint64_t residue = 0;
    uint8_t unknown_high_bits = 0;
};

[[nodiscard]] constexpr uint64_t subkey_multiplier(uint16_t subkey) noexcept {
    return ((static_cast<uint64_t>(subkey) << 16u) |
            static_cast<uint16_t>(~subkey + 2u)) & Mask56;
}

[[nodiscard]] constexpr uint64_t inverse_odd(uint64_t value) noexcept {
    uint64_t inverse = 1;
    for (int iteration = 0; iteration < 6; ++iteration) {
        inverse *= 2u - value * inverse;
    }
    return inverse;
}

[[nodiscard]] std::optional<BaseClass> normalize_base_key(uint64_t effective, uint16_t subkey) noexcept {
    if (subkey == 0) {
        return BaseClass{.residue = effective & Mask56, .unknown_high_bits = 0};
    }
    const uint64_t multiplier = subkey_multiplier(subkey);
    const uint8_t trailing = static_cast<uint8_t>(std::countr_zero(multiplier));
    const uint64_t divisor = uint64_t{1} << trailing;
    if ((effective & (divisor - 1u)) != 0) {
        return std::nullopt;
    }
    const uint8_t known_bits = static_cast<uint8_t>(56u - trailing);
    const uint64_t mask = (uint64_t{1} << known_bits) - 1u;
    const uint64_t residue = ((effective >> trailing) * inverse_odd(multiplier >> trailing)) & mask;
    return BaseClass{.residue = residue, .unknown_high_bits = trailing};
}

[[nodiscard]] bool compatible(const KeyCandidate& left, const KeyCandidate& right) noexcept {
    const uint8_t shared_known_bits = static_cast<uint8_t>(
        56u - std::max(left.unknown_high_bits, right.unknown_high_bits));
    const uint64_t mask = (uint64_t{1} << shared_known_bits) - 1u;
    return (left.key & mask) == (right.key & mask);
}

[[nodiscard]] KeyCandidate intersect(KeyCandidate left, const KeyCandidate& right) noexcept {
    const bool right_is_stronger = right.unknown_high_bits < left.unknown_high_bits;
    if (right_is_stronger) {
        left.key = right.key;
        left.unknown_high_bits = right.unknown_high_bits;
        left.equivalent_count = right.equivalent_count;
    }
    const size_t combined_sources = left.source_count + right.source_count;
    left.score = combined_sources == 0
        ? 0.0f
        : static_cast<float>((static_cast<double>(left.score) * left.source_count +
                              static_cast<double>(right.score) * right.source_count) /
                             combined_sources);
    left.source_count = combined_sources;
    left.evidence_count += right.evidence_count;
    return left;
}

void rank_base_candidates(std::vector<KeyCandidate>& candidates) {
    retain_key_candidates(candidates, [](const KeyCandidate& left, const KeyCandidate& right) {
        if (left.source_count != right.source_count) {
            return left.source_count > right.source_count;
        }
        if (left.score != right.score) {
            return left.score > right.score;
        }
        if (left.evidence_count != right.evidence_count) {
            return left.evidence_count > right.evidence_count;
        }
        if (left.unknown_high_bits != right.unknown_high_bits) {
            return left.unknown_high_bits < right.unknown_high_bits;
        }
        return left.key < right.key;
    });
}

} // namespace

std::expected<KeyRecoveryResult, std::string> recover_key(
    std::span<const RecoverySource> sources,
    KeyRecoveryMode mode) {
    KeyRecoveryOptions options;
    options.mode = mode;
    return recover_key(sources, options);
}

std::expected<KeyRecoveryResult, std::string> recover_key(
    std::span<const RecoverySource> sources,
    const KeyRecoveryOptions& options) {
    if (sources.empty()) {
        return std::unexpected("HCA key recovery failed: no HCA inputs");
    }
    if (std::ranges::any_of(sources, [](const RecoverySource& source) {
            return source.hca == nullptr;
        })) {
        return std::unexpected("HCA key recovery failed: null HCA recovery source");
    }

    struct Group {
        size_t logical_group = 0;
        uint16_t subkey = 0;
        std::vector<const Hca*> hcas;
    };
    std::vector<Group> groups;
    for (const auto& source : sources) {
        const size_t logical_group = options.mode == KeyRecoveryMode::SharedBaseKey ? 0 : source.group;
        auto group = std::ranges::find_if(groups, [&](const Group& current) {
            return current.logical_group == logical_group && current.subkey == source.subkey;
        });
        if (group == groups.end()) {
            groups.push_back(Group{
                .logical_group = logical_group,
                .subkey = source.subkey,
                .hcas = {},
            });
            group = std::prev(groups.end());
        }
        group->hcas.push_back(source.hca);
    }
    if (options.mode == KeyRecoveryMode::SharedBaseKey) {
        std::ranges::stable_sort(groups, [](const Group& left, const Group& right) {
            const auto exact_class = [](uint16_t subkey) {
                return subkey == 0 || (subkey_multiplier(subkey) & 1u) != 0;
            };
            if (exact_class(left.subkey) != exact_class(right.subkey)) {
                return exact_class(left.subkey);
            }
            return left.hcas.size() > right.hcas.size();
        });
    }

    struct GroupRecovery {
        std::vector<KeyCandidate> candidates;
        std::string error;
    };
    std::vector<GroupRecovery> recovered_groups(groups.size());
    std::atomic_size_t next_group = 0;
    std::atomic_size_t completed_groups = 0;
    std::atomic_size_t resolved_groups = 0;
    std::mutex progress_mutex;

    const auto publish_progress = [&](size_t completed) {
        if (!options.progress) {
            return;
        }
        const std::scoped_lock lock(progress_mutex);
        options.progress(KeyRecoveryProgress{
            .stage = KeyRecoveryStage::Recovering,
            .completed = completed,
            .total = groups.size(),
            .source_count = sources.size(),
            .resolved_groups = resolved_groups.load(std::memory_order_relaxed),
        });
    };
    publish_progress(0);

    const bool fast_shared_pass = options.mode == KeyRecoveryMode::SharedBaseKey && groups.size() > 1;
    const auto recover_group = [&](size_t index, bool allow_joint_refine, bool report_progress) {
        const auto& group = groups[index];
        GroupRecovery result;
        auto effective = recover_effective_key(group.hcas, allow_joint_refine);
        if (!effective) {
            result.error = std::move(effective.error());
        } else {
            for (const auto& candidate : effective->candidates) {
                const auto base = normalize_base_key(candidate.key, group.subkey);
                if (!base) {
                    continue;
                }
                result.candidates.push_back(KeyCandidate{
                    .key = base->residue,
                    .score = candidate.score,
                    .source_count = candidate.source_count,
                    .evidence_count = candidate.evidence_count,
                    .unknown_high_bits = base->unknown_high_bits,
                    .equivalent_count = uint32_t{1} << base->unknown_high_bits,
                });
            }
            if (result.candidates.empty()) {
                result.error = "HCA key recovery failed: effective candidates are incompatible with the AWB subkey";
            } else {
                rank_base_candidates(result.candidates);
                resolved_groups.fetch_add(1, std::memory_order_relaxed);
            }
        }
        recovered_groups[index] = std::move(result);
        if (report_progress) {
            const auto completed = completed_groups.fetch_add(1, std::memory_order_relaxed) + 1;
            publish_progress(completed);
        }
    };

    const auto worker = [&] {
        while (!options.stop_token.stop_requested()) {
            const auto index = next_group.fetch_add(1, std::memory_order_relaxed);
            if (index >= groups.size()) {
                return;
            }
            recover_group(index, !fast_shared_pass, true);
        }
    };
    const auto hardware_threads = std::max(1u, std::thread::hardware_concurrency());
    const size_t requested_workers = options.worker_count == 0
        ? std::min<size_t>(4, hardware_threads)
        : options.worker_count;
    const size_t worker_count = std::max<size_t>(1, std::min(groups.size(), requested_workers));
    run_workers(worker_count, worker);
    if (options.stop_token.stop_requested()) {
        return std::unexpected("HCA key recovery canceled");
    }

    std::optional<uint64_t> exact_consensus_key;
    if (fast_shared_pass) {
        for (size_t left = 0; left < recovered_groups.size() && !exact_consensus_key; ++left) {
            for (const auto& candidate : recovered_groups[left].candidates) {
                if (candidate.score < 1.0f || candidate.unknown_high_bits != 0) {
                    continue;
                }
                for (size_t right = left + 1; right < recovered_groups.size(); ++right) {
                    const bool confirmed = std::ranges::any_of(
                        recovered_groups[right].candidates,
                        [&](const KeyCandidate& other) {
                            return other.score >= 1.0f && other.unknown_high_bits == 0
                                && other.key == candidate.key;
                        });
                    if (confirmed) {
                        exact_consensus_key = candidate.key;
                        break;
                    }
                }
            }
        }
    }

    if (exact_consensus_key) {
        const auto validate_group = [&](size_t index) -> std::expected<std::optional<KeyCandidate>, std::string> {
            const auto& group = groups[index];
            std::vector<Payload> payloads;
            payloads.reserve(group.hcas.size());
            for (const Hca* hca : group.hcas) {
                payloads.push_back(Payload{
                    .bytes = HcaRecoveryAccess::source_bytes(*hca),
                    .header = &hca->header(),
                });
            }
            const auto validation = sample_frames(payloads, ValidationFrameLimit);
            if (validation.empty()) {
                return std::optional<KeyCandidate>{};
            }
            const uint64_t effective = detail::apply_subkey(
                *exact_consensus_key, group.subkey) & Mask56;
            const auto metrics = score_q((effective - 1u) & Mask56, validation);
            if (!perfect(metrics)) {
                return std::optional<KeyCandidate>{};
            }
            return std::optional<KeyCandidate>{KeyCandidate{
                .key = *exact_consensus_key,
                .score = validation_score(metrics),
                .source_count = group.hcas.size(),
                .evidence_count = metrics.tested,
            }};
        };

        std::atomic_size_t next_validation = 0;
        const auto validation_worker = [&] {
            while (!options.stop_token.stop_requested()) {
                const auto index = next_validation.fetch_add(1, std::memory_order_relaxed);
                if (index >= groups.size()) {
                    return;
                }
                auto validated = validate_group(index);
                if (validated && *validated) {
                    auto candidate = std::move(**validated);
                    recovered_groups[index] = GroupRecovery{
                        .candidates = {std::move(candidate)},
                        .error = {},
                    };
                }
            }
        };
        run_workers(worker_count, validation_worker);
        if (options.stop_token.stop_requested()) {
            return std::unexpected("HCA key recovery canceled");
        }
    }

    if (fast_shared_pass && !exact_consensus_key) {
        bool has_consensus = false;
        for (size_t left = 0; left < recovered_groups.size() && !has_consensus; ++left) {
            for (size_t right = left + 1; right < recovered_groups.size() && !has_consensus; ++right) {
                has_consensus = std::ranges::any_of(
                    recovered_groups[left].candidates,
                    [&](const KeyCandidate& left_candidate) {
                        return std::ranges::any_of(
                            recovered_groups[right].candidates,
                            [&](const KeyCandidate& right_candidate) {
                                return compatible(left_candidate, right_candidate);
                            });
                    });
            }
        }
        if (!has_consensus) {
            std::vector<size_t> retry_indices;
            for (size_t index = 0; index < recovered_groups.size(); ++index) {
                const auto& candidates = recovered_groups[index].candidates;
                if (candidates.empty() || candidates.front().score < 1.0f) {
                    retry_indices.push_back(index);
                }
            }
            std::atomic_size_t next_retry = 0;
            const auto retry_worker = [&] {
                while (!options.stop_token.stop_requested()) {
                    const auto position = next_retry.fetch_add(1, std::memory_order_relaxed);
                    if (position >= retry_indices.size()) {
                        return;
                    }
                    recover_group(retry_indices[position], true, false);
                }
            };
            const auto retry_worker_count = std::max<size_t>(
                1, std::min(retry_indices.size(), worker_count));
            run_workers(retry_worker_count, retry_worker);
            if (options.stop_token.stop_requested()) {
                return std::unexpected("HCA key recovery canceled");
            }
        }
    }

    std::vector<size_t> resolved_indices;
    resolved_indices.reserve(recovered_groups.size());
    size_t evidence_count = 0;
    std::string first_error;
    for (size_t index = 0; index < recovered_groups.size(); ++index) {
        const auto& recovered = recovered_groups[index];
        if (recovered.candidates.empty()) {
            if (first_error.empty() && !recovered.error.empty()) {
                first_error = recovered.error;
            }
            continue;
        }
        resolved_indices.push_back(index);
        evidence_count += recovered.candidates.front().evidence_count;
    }
    if (resolved_indices.empty()) {
        return std::unexpected(first_error.empty()
            ? "HCA key recovery failed: no base-key candidate could be constructed"
            : std::move(first_error));
    }

    std::vector<KeyCandidate> combined;
    if (options.mode == KeyRecoveryMode::SharedBaseKey) {
        std::ranges::sort(resolved_indices, [&](size_t left, size_t right) {
            const auto& left_candidate = recovered_groups[left].candidates.front();
            const auto& right_candidate = recovered_groups[right].candidates.front();
            if (left_candidate.score != right_candidate.score) {
                return left_candidate.score > right_candidate.score;
            }
            if (left_candidate.unknown_high_bits != right_candidate.unknown_high_bits) {
                return left_candidate.unknown_high_bits < right_candidate.unknown_high_bits;
            }
            return left_candidate.source_count > right_candidate.source_count;
        });

        struct ConsensusCandidate {
            KeyCandidate candidate;
            size_t group_support = 0;
        };
        std::vector<ConsensusCandidate> consensus;
        for (const size_t seed_group : resolved_indices) {
            for (const auto& seed : recovered_groups[seed_group].candidates) {
                ConsensusCandidate current{.candidate = seed, .group_support = 1};
                for (const size_t group_index : resolved_indices) {
                    if (group_index == seed_group) {
                        continue;
                    }
                    const auto& candidates = recovered_groups[group_index].candidates;
                    const auto match = std::ranges::find_if(candidates, [&](const KeyCandidate& candidate) {
                        return compatible(current.candidate, candidate);
                    });
                    if (match != candidates.end()) {
                        current.candidate = intersect(current.candidate, *match);
                        ++current.group_support;
                    }
                }
                const auto existing = std::ranges::find_if(consensus, [&](const ConsensusCandidate& value) {
                    return value.candidate.key == current.candidate.key &&
                        value.candidate.unknown_high_bits == current.candidate.unknown_high_bits;
                });
                if (existing == consensus.end()) {
                    consensus.push_back(std::move(current));
                } else if (current.group_support > existing->group_support ||
                           (current.group_support == existing->group_support &&
                            current.candidate.score > existing->candidate.score)) {
                    *existing = std::move(current);
                }
            }
        }
        std::ranges::sort(consensus, [](const ConsensusCandidate& left, const ConsensusCandidate& right) {
            if (left.group_support != right.group_support) {
                return left.group_support > right.group_support;
            }
            if (left.candidate.source_count != right.candidate.source_count) {
                return left.candidate.source_count > right.candidate.source_count;
            }
            if (left.candidate.score != right.candidate.score) {
                return left.candidate.score > right.candidate.score;
            }
            if (left.candidate.evidence_count != right.candidate.evidence_count) {
                return left.candidate.evidence_count > right.candidate.evidence_count;
            }
            if (left.candidate.unknown_high_bits != right.candidate.unknown_high_bits) {
                return left.candidate.unknown_high_bits < right.candidate.unknown_high_bits;
            }
            return left.candidate.key < right.candidate.key;
        });
        const auto count = std::min(MaxKeyRecoveryCandidates, consensus.size());
        combined.reserve(count);
        for (size_t index = 0; index < count; ++index) {
            combined.push_back(std::move(consensus[index].candidate));
        }
    } else {
        for (const size_t group_index : resolved_indices) {
            for (const auto& candidate : recovered_groups[group_index].candidates) {
                const auto existing = std::ranges::find_if(combined, [&](const KeyCandidate& current) {
                    return compatible(current, candidate);
                });
                if (existing == combined.end()) {
                    combined.push_back(candidate);
                } else {
                    *existing = intersect(*existing, candidate);
                }
            }
            rank_base_candidates(combined);
        }
    }

    return KeyRecoveryResult{
        .candidates = std::move(combined),
        .source_count = sources.size(),
        .evidence_count = evidence_count,
    };
}

} // namespace cricodecs::hca
