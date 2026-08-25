#include "shared/i18n.hpp"
#include "shared/adx_key_recovery.hpp"

#include "path_text.hpp"
#include "shared/embedded_entry_extractor.hpp"

#include <adx_key_recovery.hpp>
#include <adx_recovery_source_collector.hpp>
#include <ahx_key_recovery.hpp>
#include "io_reader.hpp"

#include <algorithm>
#include <cctype>
#include <span>
#include <string_view>
#include <utility>

namespace cristudio {
namespace {

[[nodiscard]] bool mentions_adx_family(std::string_view text) {
    const auto lower = lower_ascii(std::string(text));
    return lower.find("adx") != std::string::npos ||
        lower.find("ahx") != std::string::npos ||
        lower.find("aax") != std::string::npos ||
        lower.find("awb") != std::string::npos ||
        lower.find("acb") != std::string::npos ||
        lower.find("csb") != std::string::npos ||
        lower.find("cpk") != std::string::npos;
}

[[nodiscard]] bool source_might_be_adx_family(const AdxRecoverySource& source) {
    if (source.kind == AdxRecoverySource::Kind::Document) {
        return mentions_adx_family(source.format) || mentions_adx_family(source.loader_tag);
    }
    return mentions_adx_family(source.entry.type) ||
        mentions_adx_family(source.entry.detail) ||
        mentions_adx_family(source.entry.source_format) ||
        mentions_adx_family(source.entry.nested_source_format);
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> read_source(
    const AdxRecoverySource& source,
    EmbeddedEntryExtractor& extractor,
    const DecryptionKeys& keys
) {
    if (source.kind == AdxRecoverySource::Kind::Entry) {
        return extractor.extract(source.entry, keys, EmbeddedPayloadPurpose::Raw);
    }
    return cricodecs::io::read_file_bytes(source.path, cristudio::i18n::translate_utf8("Shared.AdxKeyRecovery", "ADX/AHX key recovery failed"));
}

} // namespace

AdxRecoverySource make_adx_recovery_source(const LoadedDocument& document) {
    return recovery_source(document);
}

AdxRecoverySource make_adx_recovery_source(const EntrySummary& entry) {
    return recovery_source(compact_recovery_entry(entry));
}

std::expected<AdxKeyRecoveryResult, std::string> recover_adx_key(
    std::span<const AdxRecoverySource> inputs,
    AdxRecoveryKind kind,
    const DecryptionKeys& keys
) {
    std::vector<std::vector<uint8_t>> bytes;
    EmbeddedEntryExtractor extractor;
    for (const auto& input : inputs) {
        if (!source_might_be_adx_family(input)) {
            continue;
        }
        std::expected<std::vector<std::vector<uint8_t>>, std::string> collected =
            std::unexpected(cristudio::i18n::translate_utf8("Shared.AdxKeyRecovery", "uninitialized recovery source"));
        const auto stream_kind = kind == AdxRecoveryKind::Ahx
            ? cricodecs::adx::RecoveryStreamKind::Ahx
            : cricodecs::adx::RecoveryStreamKind::Adx;
        if (input.kind == AdxRecoverySource::Kind::Document) {
            collected = cricodecs::adx::collect_recovery_streams(input.path, stream_kind);
        } else {
            auto source = read_source(input, extractor, keys);
            if (!source) {
                return std::unexpected(cristudio::i18n::translate_utf8("Shared.AdxKeyRecovery", "ADX/AHX key recovery failed: ") + source.error());
            }
            collected = cricodecs::adx::collect_recovery_streams(*source, stream_kind);
        }
        if (!collected) {
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.AdxKeyRecovery", "ADX/AHX key recovery failed: ") + collected.error());
        }
        bytes.insert(
            bytes.end(),
            std::make_move_iterator(collected->begin()),
            std::make_move_iterator(collected->end()));
    }

    const auto label = kind == AdxRecoveryKind::Ahx ? std::string_view("AHX") : std::string_view("ADX");
    if (bytes.empty()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.AdxKeyRecovery", "No encrypted ") + std::string(label) + cristudio::i18n::translate_utf8("Shared.AdxKeyRecovery", " streams were found in the selected files."));
    }

    if (kind == AdxRecoveryKind::Adx) {
        std::vector<cricodecs::adx::AdxRecoverySource> sources;
        sources.reserve(bytes.size());
        for (const auto& source : bytes) {
            sources.push_back({.bytes = source});
        }
        auto recovered = cricodecs::adx::recover_key(sources);
        if (!recovered) {
            return std::unexpected(recovered.error());
        }
        AdxKeyRecoveryResult result;
        result.start = recovered->key.xor_value;
        result.mult = recovered->key.mult;
        result.add = recovered->key.add;
        result.encryption_type = recovered->encryption_type;
        result.score = recovered->score;
        result.source_count = sources.size();
        result.total_frames = recovered->total_frames;
        result.examined_frames = recovered->examined_frames;
        result.evidence_frames = recovered->evidence_frames;
        result.source_frames = std::move(recovered->source_frames);
        result.canonical_type9_code = recovered->canonical_type9_code;
        result.candidates.reserve(recovered->candidates.size());
        for (const auto& candidate : recovered->candidates) {
            result.candidates.push_back(AdxKeyCandidateResult{
                .start = candidate.key.xor_value,
                .mult = candidate.key.mult,
                .add = candidate.key.add,
                .score = candidate.score,
                .source_count = candidate.source_count,
                .evidence_count = candidate.evidence_count,
                .canonical_type9_code = candidate.canonical_type9_code,
            });
        }
        return result;
    }

    std::vector<cricodecs::ahx::AhxRecoverySource> sources;
    sources.reserve(bytes.size());
    for (const auto& source : bytes) {
        sources.push_back({.bytes = source});
    }
    auto recovered = cricodecs::ahx::recover_key(sources);
    if (!recovered) {
        return std::unexpected(recovered.error());
    }
    AdxKeyRecoveryResult result;
    result.start = recovered->key.start;
    result.mult = recovered->key.mult;
    result.add = recovered->key.add;
    result.encryption_type = recovered->encryption_type;
    result.score = recovered->score;
    result.source_count = sources.size();
    result.total_frames = recovered->total_frames;
    result.evidence_frames = recovered->evidence_frames;
    result.source_frames = std::move(recovered->source_frames);
    result.component_frames = recovered->component_frames;
    result.candidate_counts = recovered->candidate_counts;
    result.canonical_type9_code = recovered->canonical_type9_code;
    result.candidates.reserve(recovered->candidates.size());
    for (const auto& candidate : recovered->candidates) {
        result.candidates.push_back(AdxKeyCandidateResult{
            .start = candidate.key.start,
            .mult = candidate.key.mult,
            .add = candidate.key.add,
            .score = candidate.score,
            .source_count = candidate.source_count,
            .evidence_count = candidate.evidence_count,
            .candidate_counts = candidate.candidate_counts,
            .canonical_type9_code = candidate.canonical_type9_code,
        });
    }
    return result;
}

} // namespace cristudio
