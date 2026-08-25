#include "shared/i18n.hpp"
#include "modules/acx/acx_browse.hpp"

#include "path_text.hpp"
#include "shared/document_helpers.hpp"

#include <utility>

namespace cristudio::modules::acx {
namespace {

std::string entry_type(const cricodecs::acx::AcxEntry& entry) {
    switch (entry.type) {
    case cricodecs::acx::AcxEntryType::adx:
        return "ADX";
    case cricodecs::acx::AcxEntryType::ogg:
        return cristudio::i18n::translate_utf8("Acx.AcxBrowse", "Ogg Vorbis");
    case cricodecs::acx::AcxEntryType::unknown:
        break;
    }
    return "data";
}

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::acx::AcxContainer& acx) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Acx.AcxBrowse", "ACX audio archive"));
    doc.info.push_back(translated_info_row("Acx.AcxBrowse", "Entries", number(acx.entry_count())));
    doc.info.push_back(translated_info_row("Acx.AcxBrowse", "Table size", number(acx.table_size())));
    doc.info.push_back(translated_info_row("Acx.AcxBrowse", "First payload offset", acx.first_payload_offset() ? hex_number(*acx.first_payload_offset()) : "-"));
    doc.info.push_back(translated_info_row("Acx.AcxBrowse", "Payload end offset", acx.payload_end_offset() ? hex_number(*acx.payload_end_offset()) : "-"));
    doc.info.push_back(translated_info_row("Acx.AcxBrowse", "ADX entries", number(acx.type_count(cricodecs::acx::AcxEntryType::adx))));
    doc.info.push_back(translated_info_row("Acx.AcxBrowse", "Ogg entries", number(acx.type_count(cricodecs::acx::AcxEntryType::ogg))));
    doc.info.push_back(translated_info_row("Acx.AcxBrowse", "Unknown entries", number(acx.type_count(cricodecs::acx::AcxEntryType::unknown))));

    doc.entries.reserve(acx.entries().size());
    for (const auto& entry : acx.entries()) {
        doc.entries.push_back(source_entry({
            archive_display_path(entry.suggested_path().generic_string()),
            entry_type(entry),
            byte_count(entry.size),
            number(entry.offset),
            "index " + number(entry.index) + cristudio::i18n::translate_utf8("Acx.AcxBrowse", ", table row ") + hex_number(0x08ull + static_cast<uint64_t>(entry.index) * 0x08ull)
        }, path, "ACX", entry.index));
    }
    return doc;
}

} // namespace cristudio::modules::acx
