#pragma once

#include "shared/key_recovery_source.hpp"
#include <hca_key_recovery.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace cristudio {

using HcaRecoverySource = KeyRecoverySource;

struct HcaKeyRecoveryResult {
    cricodecs::hca::KeyRecoveryResult recovery;
    size_t hca_count = 0;
};

[[nodiscard]] HcaRecoverySource make_hca_recovery_source(const LoadedDocument& document);
[[nodiscard]] HcaRecoverySource make_hca_recovery_source(const EntrySummary& entry);
[[nodiscard]] bool supports_hca_key_recovery(const LoadedDocument& document);
[[nodiscard]] bool supports_hca_key_recovery(const EntrySummary& entry);

[[nodiscard]] std::expected<HcaKeyRecoveryResult, std::string> recover_hca_key(
    std::span<const HcaRecoverySource> sources,
    const DecryptionKeys& keys = {},
    cricodecs::KeyRecoveryMode mode = cricodecs::KeyRecoveryMode::SharedBaseKey
);

[[nodiscard]] std::expected<HcaKeyRecoveryResult, std::string> recover_hca_key(
    std::span<const HcaRecoverySource> sources,
    const DecryptionKeys& keys,
    const cricodecs::hca::KeyRecoveryOptions& options
);

} // namespace cristudio
