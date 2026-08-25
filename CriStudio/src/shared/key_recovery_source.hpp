#pragma once

#include "document/document_types.hpp"

#include <cstdint>
#include <utility>

namespace cristudio {

struct KeyRecoverySource {
    enum class Kind : uint8_t {
        Document,
        Entry,
    };

    Kind kind = Kind::Document;
    std::filesystem::path path;
    std::string name;
    std::string format;
    std::string loader_tag;
    EntrySummary entry;
};

[[nodiscard]] inline KeyRecoverySource recovery_source(const LoadedDocument& document) {
    return {
        .kind = KeyRecoverySource::Kind::Document,
        .path = document.path,
        .name = document.display_name,
        .format = std::string(document_format_id(document)),
        .loader_tag = document.loader_tag,
    };
}

[[nodiscard]] inline KeyRecoverySource recovery_source(EntrySummary entry) {
    const auto name = entry.name;
    return {
        .kind = KeyRecoverySource::Kind::Entry,
        .name = name,
        .entry = std::move(entry),
    };
}

[[nodiscard]] inline EntrySummary compact_recovery_entry(const EntrySummary& entry) {
    return {
        .name = entry.name,
        .type = entry.type,
        .detail = entry.detail,
        .source_path = entry.source_path,
        .source_format = entry.source_format,
        .source_index = entry.source_index,
        .has_source = entry.has_source,
        .nested_source_format = entry.nested_source_format,
        .nested_source_index = entry.nested_source_index,
        .has_nested_source = entry.has_nested_source,
        .hca_subkey = entry.hca_subkey,
    };
}

} // namespace cristudio
