#include "shared/i18n.hpp"
#include "modules/cvm/cvm_browse.hpp"

#include "shared/document_helpers.hpp"

#include <utility>

namespace cristudio::modules::cvm {
LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::cvm::CvmContainer& cvm) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Cvm.CvmBrowse", "CVM/ROFS image"));
    const auto& header = cvm.header();
    const auto& zone = cvm.zone();
    const auto& primary = cvm.primary_volume();
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Disc name", cvm.disc_name()));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Recording date", cvm.recording_date_text()));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Media", cvm.media()));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Scrambled", bool_text(cvm.is_scrambled())));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Entries", number(cvm.entry_count())));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Accessible", bool_text(cvm.has_accessible_contents())));
    if (cvm.is_scrambled() && !cvm.has_accessible_contents()) {
        doc.info.push_back(translated_info_row_with_value(
            "Cvm.CvmBrowse",
            "Key",
            "Cvm.CvmBrowse",
            "required for TOC",
            "Key",
            "required for TOC"));
    }
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Flags", hex_number(header.flags)));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Filesystem", header.filesystem_id));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Maker", header.maker_id));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Zone sector", number(zone.zone_sector)));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Data sector", number(zone.data_sector)));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "ISO sector", number(zone.iso_sector)));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "ISO offset", number(cvm.embedded_iso_offset())));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "ISO size", byte_count(cvm.embedded_iso_size())));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "ISO sectors", number(cvm.embedded_iso_sector_count())));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "System ID", primary.system_identifier));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Volume ID", primary.volume_identifier));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Volume set", primary.volume_set_identifier));
    doc.info.push_back(translated_info_row("Cvm.CvmBrowse", "Logical block", number(primary.logical_block_size)));

    doc.entries.reserve(cvm.entries().size());
    for (const auto& entry : cvm.entries()) {
        doc.entries.push_back(source_entry({
            archive_display_path(entry.path.generic_string()),
            "file",
            byte_count(entry.size),
            "sector " + number(entry.extent_sector),
            "index " + number(entry.index)
        }, path, "CVM", entry.index));
    }
    return doc;
}

} // namespace cristudio::modules::cvm
