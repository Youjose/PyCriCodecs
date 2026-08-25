#pragma once

#include "shared/key_recovery_source.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cristudio {

using AacRecoverySource = KeyRecoverySource;

struct AacKeyCandidateResult {
    uint64_t key = 0;
    float score = 0.0f;
    size_t validated_sources = 0;
    size_t source_count = 0;
    size_t candidate_count = 0;
};

struct AacKeyRecoveryResult {
    std::vector<AacKeyCandidateResult> candidates;
    uint64_t key = 0;
    float score = 0.0f;
    size_t validated_sources = 0;
    size_t source_count = 0;
    size_t container_count = 0;
    size_t candidate_count = 0;
};

[[nodiscard]] bool supports_aac_key_recovery(const LoadedDocument& document);
[[nodiscard]] bool supports_aac_key_recovery(const EntrySummary& entry);
[[nodiscard]] AacRecoverySource make_aac_recovery_source(const LoadedDocument& document);
[[nodiscard]] AacRecoverySource make_aac_recovery_source(const EntrySummary& entry);
[[nodiscard]] std::expected<AacKeyRecoveryResult, std::string> recover_aac_key(
    std::span<const AacRecoverySource> sources);

} // namespace cristudio
