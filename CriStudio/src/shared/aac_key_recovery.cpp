#include "shared/i18n.hpp"
#include "shared/aac_key_recovery.hpp"

#include "acb_container.hpp"
#include "awb_container.hpp"
#include "path_text.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <utility>

namespace cristudio {
namespace {

[[nodiscard]] bool mentions_aac(std::string text) {
    text = lower_ascii(std::move(text));
    return text.find("aac") != std::string::npos || text.find("m4a") != std::string::npos;
}

[[nodiscard]] std::expected<cricodecs::awb::KeyRecoveryResult, std::string> recover_document(
    const AacRecoverySource& source) {
    const auto tag = lower_ascii(source.loader_tag.empty() ? source.format : source.loader_tag);
    // Loader tags are machine-readable routing identifiers; never translate.
    if (tag == "acb" || tag.find("acb cue") != std::string::npos) {
        auto acb = cricodecs::acb::AcbContainer::load(source.path);
        if (!acb) {
            return std::unexpected(acb.error());
        }
        return acb->recover_aac_key();
    }
    if (tag == "awb" || tag.find("awb audio") != std::string::npos) {
        auto awb = cricodecs::awb::AwbContainer::load(source.path);
        if (!awb) {
            return std::unexpected(awb.error());
        }
        return awb->recover_aac_key();
    }
    return std::unexpected(cristudio::i18n::translate_utf8("Shared.AacKeyRecovery", "selected document is not an ACB or AWB"));
}

[[nodiscard]] std::expected<cricodecs::awb::KeyRecoveryResult, std::string> recover_entry_group(
    const std::filesystem::path& path,
    std::string_view format,
    std::span<const uint32_t> indices) {
    if (format == "acb") {
        auto acb = cricodecs::acb::AcbContainer::load(path);
        if (!acb) {
            return std::unexpected(acb.error());
        }

        std::vector<std::vector<uint8_t>> payloads;
        payloads.reserve(indices.size());
        for (const uint32_t index : indices) {
            if (index >= acb->waveform_count() || acb->waveform(index).encode_type != 19) {
                return std::unexpected(cristudio::i18n::translate_utf8("Shared.AacKeyRecovery", "selected ACB waveform is not AAC/M4A"));
            }
            auto bytes = acb->extract_waveform_data(index);
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            payloads.push_back(std::move(*bytes));
        }
        std::vector<cricodecs::awb::AacRecoverySource> sources;
        sources.reserve(payloads.size());
        for (const auto& payload : payloads) {
            sources.push_back({payload});
        }
        return cricodecs::awb::recover_aac_key(sources);
    }
    if (format == "awb") {
        auto awb = cricodecs::awb::AwbContainer::load(path);
        if (!awb) {
            return std::unexpected(awb.error());
        }
        return awb->recover_aac_key(indices);
    }
    return std::unexpected(cristudio::i18n::translate_utf8("Shared.AacKeyRecovery", "selected entry is not an ACB/AWB AAC source"));
}

} // namespace

bool supports_aac_key_recovery(const LoadedDocument& document) {
    const auto tag = lower_ascii(std::string(document_format_id(document)));
    // Loader tags are machine-readable routing identifiers; never translate.
    if (tag == "acb" || tag.find("acb cue") != std::string::npos) {
        return std::ranges::any_of(document.entries, [](const EntrySummary& entry) {
            return mentions_aac(entry.type) || mentions_aac(entry.name);
        });
    }
    if (tag == "awb" || tag.find("awb audio") != std::string::npos) {
        auto awb = cricodecs::awb::AwbContainer::load(document.path);
        return awb && awb->has_aac_key_recovery_candidates();
    }
    return false;
}

bool supports_aac_key_recovery(const EntrySummary& entry) {
    const auto format = lower_ascii(entry.has_nested_source
        ? entry.nested_source_format
        : entry.source_format);
    if (format == "acb") {
        return mentions_aac(entry.type) || mentions_aac(entry.name);
    }
    if (format != "awb" || !entry.has_source) {
        return false;
    }
    auto awb = cricodecs::awb::AwbContainer::load(entry.source_path);
    const uint32_t index = entry.has_nested_source ? entry.nested_source_index : entry.source_index;
    return awb && awb->has_aac_key_recovery_candidate(index);
}

AacRecoverySource make_aac_recovery_source(const LoadedDocument& document) {
    return recovery_source(document);
}

AacRecoverySource make_aac_recovery_source(const EntrySummary& entry) {
    return recovery_source(entry);
}

std::expected<AacKeyRecoveryResult, std::string> recover_aac_key(
    std::span<const AacRecoverySource> sources) {
    struct Aggregate {
        cricodecs::awb::KeyCandidate candidate;
        double weighted_score = 0.0;
        size_t container_count = 0;
    };
    std::map<uint64_t, Aggregate> aggregated;
    size_t container_count = 0;
    const auto append = [&](const std::expected<cricodecs::awb::KeyRecoveryResult, std::string>& recovered)
        -> std::expected<void, std::string> {
        if (!recovered) {
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.AacKeyRecovery", "AAC key recovery failed: ") + recovered.error());
        }
        if (recovered->candidates.empty()) {
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.AacKeyRecovery", "AAC key recovery failed: no candidates were returned"));
        }
        for (const auto& candidate : recovered->candidates) {
            auto& aggregate = aggregated[candidate.key];
            if (aggregate.container_count == 0u) {
                aggregate.candidate = candidate;
                aggregate.candidate.score = 0.0f;
                aggregate.candidate.validated_sources = 0;
                aggregate.candidate.source_count = 0;
                aggregate.candidate.candidate_count = 0;
            }
            aggregate.weighted_score += static_cast<double>(candidate.score) * candidate.source_count;
            aggregate.candidate.validated_sources += candidate.validated_sources;
            aggregate.candidate.source_count += candidate.source_count;
            aggregate.candidate.candidate_count += candidate.candidate_count;
            ++aggregate.container_count;
        }
        ++container_count;
        return {};
    };

    using EntryGroup = std::pair<std::filesystem::path, std::string>;
    std::map<EntryGroup, std::vector<uint32_t>> entry_groups;
    for (const auto& source : sources) {
        if (source.kind == AacRecoverySource::Kind::Document) {
            if (auto appended = append(recover_document(source)); !appended) {
                return std::unexpected(appended.error());
            }
            continue;
        }

        const auto& entry = source.entry;
        auto format = lower_ascii(entry.has_nested_source
            ? entry.nested_source_format
            : entry.source_format);
        const uint32_t index = entry.has_nested_source ? entry.nested_source_index : entry.source_index;
        auto& indices = entry_groups[{entry.source_path, std::move(format)}];
        if (std::ranges::find(indices, index) == indices.end()) {
            indices.push_back(index);
        }
    }
    for (const auto& [group, indices] : entry_groups) {
        const auto& [path, format] = group;
        if (auto appended = append(recover_entry_group(path, format, indices)); !appended) {
            return std::unexpected(appended.error());
        }
    }
    std::vector<AacKeyCandidateResult> candidates;
    for (auto& [key, aggregate] : aggregated) {
        if (aggregate.container_count != container_count) continue;
        aggregate.candidate.score = aggregate.candidate.source_count == 0u
            ? 0.0f
            : static_cast<float>(aggregate.weighted_score / aggregate.candidate.source_count);
        candidates.push_back(AacKeyCandidateResult{
            .key = key,
            .score = aggregate.candidate.score,
            .validated_sources = aggregate.candidate.validated_sources,
            .source_count = aggregate.candidate.source_count,
            .candidate_count = aggregate.candidate.candidate_count,
        });
    }
    if (candidates.empty()) {
        if (!aggregated.empty()) {
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.AacKeyRecovery", "AAC key recovery failed: selected sources do not share one effective key candidate"));
        }
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.AacKeyRecovery", "No encrypted AAC/M4A sources were selected."));
    }
    std::ranges::sort(candidates, [](const auto& left, const auto& right) {
        if (left.validated_sources != right.validated_sources) {
            return left.validated_sources > right.validated_sources;
        }
        if (left.score != right.score) return left.score > right.score;
        return left.key < right.key;
    });
    if (candidates.size() > cricodecs::MaxKeyRecoveryCandidates) {
        candidates.resize(cricodecs::MaxKeyRecoveryCandidates);
    }
    const auto best = candidates.front();
    return AacKeyRecoveryResult{
        .candidates = std::move(candidates),
        .key = best.key,
        .score = best.score,
        .validated_sources = best.validated_sources,
        .source_count = best.source_count,
        .container_count = container_count,
        .candidate_count = best.candidate_count,
    };
}

} // namespace cristudio
