#pragma once

#include "shared/key_recovery_source.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cristudio {

enum class AdxRecoveryKind : uint8_t {
    Adx,
    Ahx,
};

using AdxRecoverySource = KeyRecoverySource;

struct AdxKeyCandidateResult {
    uint16_t start = 0;
    uint16_t mult = 0;
    uint16_t add = 0;
    float score = 0.0f;
    size_t source_count = 0;
    uint64_t evidence_count = 0;
    std::array<uint32_t, 3> candidate_counts{};
    uint64_t canonical_type9_code = 0;
};

struct AdxKeyRecoveryResult {
    std::vector<AdxKeyCandidateResult> candidates;
    uint16_t start = 0;
    uint16_t mult = 0;
    uint16_t add = 0;
    uint8_t encryption_type = 0;
    float score = 0.0f;
    size_t source_count = 0;
    uint64_t total_frames = 0;
    uint64_t examined_frames = 0;
    uint64_t evidence_frames = 0;
    std::vector<uint64_t> source_frames;
    std::array<uint64_t, 3> component_frames{};
    std::array<uint32_t, 3> candidate_counts{};
    uint64_t canonical_type9_code = 0;
};

[[nodiscard]] AdxRecoverySource make_adx_recovery_source(const LoadedDocument& document);
[[nodiscard]] AdxRecoverySource make_adx_recovery_source(const EntrySummary& entry);

[[nodiscard]] std::expected<AdxKeyRecoveryResult, std::string> recover_adx_key(
    std::span<const AdxRecoverySource> sources,
    AdxRecoveryKind kind,
    const DecryptionKeys& keys = {}
);

} // namespace cristudio
