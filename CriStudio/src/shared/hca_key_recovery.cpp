#include "shared/i18n.hpp"
#include "shared/hca_key_recovery.hpp"

#include "path_text.hpp"
#include "shared/embedded_entry_extractor.hpp"

#include "acb_container.hpp"
#include "awb_container.hpp"
#include "cpk_container.hpp"
#include "hca_codec.hpp"
#include "usm_container.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace cristudio {
namespace {

struct CollectedHca {
    cricodecs::hca::Hca hca;
    uint16_t subkey = 0;
    size_t group = 0;
};

[[nodiscard]] bool text_supports_hca(std::string text) {
    const auto lowered = lower_ascii(std::move(text));
    // Format-identification tokens used for matching; never translate.
    return lowered == "hca" ||
        lowered.find("hca audio") != std::string::npos ||
        lowered.find(".hca") != std::string::npos;
}

[[nodiscard]] bool entry_tree_supports_hca(const EntrySummary& entry) {
    if (text_supports_hca(
            entry.name + " " + entry.type + " " + entry.source_format + " " + entry.nested_source_format)) {
        return true;
    }
    return std::ranges::any_of(entry.inspector_entries, entry_tree_supports_hca);
}

[[nodiscard]] bool has_magic(std::span<const uint8_t> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() &&
        std::equal(magic.begin(), magic.end(), bytes.begin());
}

[[nodiscard]] bool is_hca(std::span<const uint8_t> bytes) {
    return bytes.size() >= 4 &&
        (bytes[0] & 0x7Fu) == 'H' &&
        (bytes[1] & 0x7Fu) == 'C' &&
        (bytes[2] & 0x7Fu) == 'A' &&
        bytes[3] == 0;
}

[[nodiscard]] std::expected<void, std::string> append_hca(
    std::span<const uint8_t> bytes,
    std::string_view context,
    uint16_t subkey,
    size_t group,
    std::vector<CollectedHca>& sources
) {
    auto hca = cricodecs::hca::Hca::load(bytes);
    if (!hca) {
        return std::unexpected(std::string(context) + ": " + hca.error());
    }
    if (hca->header().cipher.type == 56) {
        sources.push_back(CollectedHca{
            .hca = std::move(*hca),
            .subkey = subkey,
            .group = group,
        });
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> append_awb(
    const cricodecs::awb::AwbContainer& awb,
    std::string_view context,
    size_t group,
    std::vector<CollectedHca>& sources
) {
    for (uint32_t index = 0; index < awb.file_count(); ++index) {
        auto bytes = awb.file_data(index);
        if (!bytes) {
            return std::unexpected(
                std::string(context) + cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", ": AWB entry ") + std::to_string(index)
                + cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", " could not be read: ") + bytes.error());
        }
        if (!is_hca(*bytes)) {
            continue;
        }
        if (auto appended = append_hca(
                *bytes,
                std::string(context) + cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", ": AWB entry ") + std::to_string(index),
                awb.subkey(),
                group,
                sources);
            !appended) {
            return appended;
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> append_acb(
    const cricodecs::acb::AcbContainer& acb,
    std::string_view context,
    size_t group,
    std::vector<CollectedHca>& sources
) {
    auto awb = acb.load_awb();
    if (!awb) {
        return std::unexpected(std::string(context) + ": " + awb.error());
    }
    return append_awb(*awb, context, group, sources);
}

[[nodiscard]] std::expected<void, std::string> append_usm(
    cricodecs::usm::UsmReader& usm,
    std::string_view context,
    size_t group,
    std::vector<CollectedHca>& sources
) {
    for (size_t index = 0; index < usm.streams().size(); ++index) {
        if (usm.streams()[index].stream_id != cricodecs::usm::UsmChunkType::SFA) {
            continue;
        }
        auto bytes = usm.extract_stream(static_cast<uint32_t>(index));
        if (!bytes) {
            return std::unexpected(
                std::string(context) + cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", ": USM audio stream ") + std::to_string(index)
                + cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", " could not be extracted: ") + bytes.error());
        }
        if (!is_hca(*bytes)) {
            continue;
        }
        if (auto appended = append_hca(
                *bytes,
                std::string(context) + cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", ": USM audio stream ") + std::to_string(index),
                0,
                group,
                sources);
            !appended) {
            return appended;
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> append_payload(
    std::span<const uint8_t> bytes,
    std::string_view context,
    uint16_t subkey,
    size_t group,
    std::vector<CollectedHca>& sources
) {
    if (is_hca(bytes)) {
        return append_hca(bytes, context, subkey, group, sources);
    }
    if (has_magic(bytes, "AFS2")) {
        auto awb = cricodecs::awb::AwbContainer::load(bytes);
        if (!awb) {
            return std::unexpected(std::string(context) + ": " + awb.error());
        }
        return append_awb(*awb, context, group, sources);
    }
    if (has_magic(bytes, "CRID") || has_magic(bytes, "SFSH")) {
        cricodecs::usm::UsmReader usm;
        if (auto loaded = usm.load(bytes); !loaded) {
            return std::unexpected(std::string(context) + ": " + loaded.error());
        }
        return append_usm(usm, context, group, sources);
    }
    if (has_magic(bytes, "@UTF")) {
        auto acb = cricodecs::acb::AcbContainer::load(bytes);
        if (acb) {
            return append_acb(*acb, context, group, sources);
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> append_cpk(
    const cricodecs::cpk::Cpk& cpk,
    std::string_view context,
    size_t& next_group,
    std::vector<CollectedHca>& sources
) {
    for (size_t index = 0; index < cpk.file_count(); ++index) {
        auto bytes = cpk.file_bytes(index);
        if (!bytes) {
            return std::unexpected(
                std::string(context) + cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", ": CPK entry ") + std::to_string(index)
                + cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", " could not be read: ") + bytes.error());
        }
        const auto& entry = cpk.files()[index];
        const auto entry_context = std::string(context) + ": " + entry.full_path().generic_string();
        const auto entry_group = next_group++;
        if (auto appended = append_payload(*bytes, entry_context, 0, entry_group, sources); !appended) {
            return appended;
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> append_document(
    const HcaRecoverySource& source,
    size_t& next_group,
    std::vector<CollectedHca>& sources
) {
    const auto tag = lower_ascii(source.loader_tag.empty() ? source.format : source.loader_tag);
    const auto context = source.name.empty()
        ? source.path.filename().generic_string()
        : source.name;

    // Loader tags are machine-readable routing identifiers; never translate.
    if (tag == "cpk" || tag.find("cpk archive") != std::string::npos) {
        auto cpk = cricodecs::cpk::Cpk::load(source.path);
        if (!cpk) {
            return std::unexpected(context + ": " + cpk.error());
        }
        return append_cpk(*cpk, context, next_group, sources);
    }

    const auto group = next_group++;

    if (tag == "hca" || tag.find("hca audio") != std::string::npos) {
        auto hca = cricodecs::hca::Hca::load(source.path);
        if (!hca) {
            return std::unexpected(context + ": " + hca.error());
        }
        if (hca->header().cipher.type == 56) {
            sources.push_back(CollectedHca{
                .hca = std::move(*hca),
                .subkey = 0,
                .group = group,
            });
        }
        return {};
    }
    if (tag == "awb" || tag.find("awb audio") != std::string::npos) {
        auto awb = cricodecs::awb::AwbContainer::load(source.path);
        if (!awb) {
            return std::unexpected(context + ": " + awb.error());
        }
        return append_awb(*awb, context, group, sources);
    }
    if (tag == "acb" || tag.find("acb cue") != std::string::npos) {
        auto acb = cricodecs::acb::AcbContainer::load(source.path);
        if (!acb) {
            return std::unexpected(context + ": " + acb.error());
        }
        return append_acb(*acb, context, group, sources);
    }
    if (tag == "usm" || tag.find("usm/") != std::string::npos) {
        cricodecs::usm::UsmReader usm;
        if (auto loaded = usm.load(source.path); !loaded) {
            return std::unexpected(context + ": " + loaded.error());
        }
        return append_usm(usm, context, group, sources);
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> append_entry(
    const EntrySummary& entry,
    EmbeddedEntryExtractor& extractor,
    const DecryptionKeys& keys,
    size_t group,
    std::vector<CollectedHca>& sources
) {
    auto bytes = extractor.extract(entry, keys, EmbeddedPayloadPurpose::Raw);
    if (!bytes) {
        return std::unexpected(entry.name + ": " + bytes.error());
    }
    const auto context = entry.name.empty() ? std::string(cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", "selected entry")) : entry.name;
    return append_payload(*bytes, context, entry.hca_subkey, group, sources);
}

} // namespace

bool supports_hca_key_recovery(const LoadedDocument& document) {
    if (text_supports_hca(std::string(document_format_id(document)))) {
        return true;
    }
    return std::ranges::any_of(document.entries, entry_tree_supports_hca);
}

bool supports_hca_key_recovery(const EntrySummary& entry) {
    return entry_tree_supports_hca(entry);
}

HcaRecoverySource make_hca_recovery_source(const LoadedDocument& document) {
    return recovery_source(document);
}

HcaRecoverySource make_hca_recovery_source(const EntrySummary& entry) {
    return recovery_source(compact_recovery_entry(entry));
}

std::expected<HcaKeyRecoveryResult, std::string> recover_hca_key(
    std::span<const HcaRecoverySource> inputs,
    const DecryptionKeys& keys,
    cricodecs::KeyRecoveryMode mode
) {
    cricodecs::hca::KeyRecoveryOptions options;
    options.mode = mode;
    return recover_hca_key(inputs, keys, options);
}

std::expected<HcaKeyRecoveryResult, std::string> recover_hca_key(
    std::span<const HcaRecoverySource> inputs,
    const DecryptionKeys& keys,
    const cricodecs::hca::KeyRecoveryOptions& options
) {
    std::vector<CollectedHca> collected;
    EmbeddedEntryExtractor extractor;
    size_t next_group = 0;
    if (options.progress) {
        options.progress(cricodecs::hca::KeyRecoveryProgress{
            .stage = cricodecs::hca::KeyRecoveryStage::Collecting,
            .completed = 0,
            .total = inputs.size(),
        });
    }
    for (size_t index = 0; index < inputs.size(); ++index) {
        if (options.stop_token.stop_requested()) {
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", "HCA key recovery canceled"));
        }
        const auto& input = inputs[index];
        const auto appended = input.kind == HcaRecoverySource::Kind::Document
            ? append_document(input, next_group, collected)
            : append_entry(input.entry, extractor, keys, next_group++, collected);
        if (!appended) {
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", "HCA key recovery failed: ") + appended.error());
        }
        if (options.progress) {
            options.progress(cricodecs::hca::KeyRecoveryProgress{
                .stage = cricodecs::hca::KeyRecoveryStage::Collecting,
                .completed = index + 1,
                .total = inputs.size(),
                .source_count = collected.size(),
            });
        }
    }

    if (collected.empty()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.HcaKeyRecovery", "No cipher type-56 HCA streams were found in the selected files."));
    }
    std::vector<cricodecs::hca::RecoverySource> sources;
    sources.reserve(collected.size());
    for (const auto& source : collected) {
        sources.push_back(cricodecs::hca::RecoverySource{
            .hca = &source.hca,
            .subkey = source.subkey,
            .group = source.group,
        });
    }
    const bool single_cpk_document = inputs.size() == 1 &&
        inputs.front().kind == HcaRecoverySource::Kind::Document &&
        lower_ascii(inputs.front().loader_tag.empty() ? inputs.front().format : inputs.front().loader_tag)
            .find("cpk") != std::string::npos;
    auto core_options = options;
    if (single_cpk_document) {
        core_options.mode = cricodecs::KeyRecoveryMode::Independent;
    }
    auto recovered = cricodecs::hca::recover_key(sources, core_options);
    if (!recovered) {
        return std::unexpected(recovered.error());
    }
    return HcaKeyRecoveryResult{
        .recovery = std::move(*recovered),
        .hca_count = collected.size(),
    };
}

} // namespace cristudio
