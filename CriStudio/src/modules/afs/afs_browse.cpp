#include "shared/i18n.hpp"
#include "modules/afs/afs_browse.hpp"

#include "shared/document_helpers.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <span>
#include <utility>

namespace cristudio::modules::afs {
namespace {

std::string hex_bytes(std::span<const uint8_t> bytes) {
    std::ostringstream out;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(bytes[i]);
    }
    return out.str();
}

} // namespace

std::string timestamp_text(const cricodecs::afs::AfsDirectoryTimestamp& timestamp) {
    std::ostringstream out;
    out << std::setw(4) << std::setfill('0') << timestamp.year << '-'
        << std::setw(2) << std::setfill('0') << timestamp.month << '-'
        << std::setw(2) << std::setfill('0') << timestamp.day << ' '
        << std::setw(2) << std::setfill('0') << timestamp.hour << ':'
        << std::setw(2) << std::setfill('0') << timestamp.minute << ':'
        << std::setw(2) << std::setfill('0') << timestamp.second;
    return out.str();
}

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::afs::AfsContainer& afs) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Afs.AfsBrowse", "AFS archive"));
    doc.info.push_back(translated_info_row("Afs.AfsBrowse", "Entries", number(afs.entry_count())));
    doc.info.push_back(translated_info_row("Afs.AfsBrowse", "Present entries", number(afs.present_entry_count())));
    doc.info.push_back(translated_info_row("Afs.AfsBrowse", "Alignment", number(afs.alignment())));
    doc.info.push_back(translated_info_row("Afs.AfsBrowse", "Directory table", bool_text(afs.has_directory_table())));
    doc.info.push_back(translated_info_row("Afs.AfsBrowse", "Directory table on build", bool_text(afs.directory_table_enabled())));
    doc.info.push_back(translated_info_row("Afs.AfsBrowse", "Directory offset", afs.directory_table_offset() ? hex_number(*afs.directory_table_offset()) : "-"));
    doc.info.push_back(translated_info_row("Afs.AfsBrowse", "Directory size", afs.directory_table_size() ? byte_count(*afs.directory_table_size()) : "-"));
    doc.info.push_back(translated_info_row("Afs.AfsBrowse", "First payload offset", afs.first_payload_offset() ? hex_number(*afs.first_payload_offset()) : "-"));

    doc.entries.reserve(afs.entries().size());
    for (const auto& entry : afs.entries()) {
        if (!entry.present) {
            EntrySummary reserved;
            reserved.name = "file_id " + number(entry.index);
            reserved.type = cristudio::i18n::translate_utf8("Afs.AfsBrowse", "reserved");
            reserved.detail = cristudio::i18n::translate_utf8("Afs.AfsBrowse", "empty file ID slot");
            reserved.cells = {reserved.name, reserved.type, {}, {}, reserved.detail};
            doc.entries.push_back(std::move(reserved));
            continue;
        }
        auto detail = "id " + number(entry.index);
        if (auto timestamp = entry.directory_timestamp()) {
            detail += cristudio::i18n::translate_utf8("Afs.AfsBrowse", ", timestamp ") + timestamp_text(*timestamp);
        }
        if (!entry.header_source_name.value_or("").empty()) {
            detail += cristudio::i18n::translate_utf8("Afs.AfsBrowse", ", header ") + *entry.header_source_name;
        }
        if (std::ranges::any_of(entry.directory_metadata, [](uint8_t value) { return value != 0; })) {
            detail += cristudio::i18n::translate_utf8("Afs.AfsBrowse", ", metadata ") + hex_bytes(entry.directory_metadata);
        }
        doc.entries.push_back(source_entry({
            archive_display_path(entry.suggested_path().generic_string()),
            cricodecs::afs::entry_extension(entry.type),
            byte_count(entry.size),
            number(entry.offset),
            detail
        }, path, "AFS", entry.index));
    }
    return doc;
}

} // namespace cristudio::modules::afs
