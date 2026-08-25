#pragma once

#include "shared/key_recovery_source.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cristudio {

using UsmRecoverySource = KeyRecoverySource;

struct UsmKeyRecoveryResult {
    std::string source;
    uint64_t key = 0;
    float score = 0.0f;
    size_t source_count = 1;
    size_t evidence_count = 0;
    size_t sample_blocks = 0;
    size_t video_chunks = 0;
    size_t audio_chunks = 0;
    float audio_score = 0.0f;
    size_t hca_streams = 0;
    float hca_score = 0.0f;
    bool hca_video_supported = false;
};

struct UsmKeyRecoveryReport {
    std::vector<UsmKeyRecoveryResult> recovered;
    std::vector<std::string> errors;
    size_t usm_count = 0;
};

[[nodiscard]] UsmRecoverySource make_usm_recovery_source(const LoadedDocument& document);
[[nodiscard]] UsmRecoverySource make_usm_recovery_source(const EntrySummary& entry);

[[nodiscard]] std::expected<UsmKeyRecoveryReport, std::string> recover_usm_keys(
    std::span<const UsmRecoverySource> sources,
    const DecryptionKeys& keys = {}
);

} // namespace cristudio
