#include "shared/i18n.hpp"
#include "modules/acb/acb_browse.hpp"
#include "modules/acb/acb_cue_view.hpp"

#include "shared/document_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace cristudio::modules::acb {
LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::acb::AcbContainer& acb) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Acb.AcbBrowse", "ACB cue sheet"));
    doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Name", std::string(acb.name())));
    doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Waveforms", number(acb.waveform_count())));
    doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Resolved names", number(static_cast<uint64_t>(acb.wave_names().size()))));
    doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Embedded AWB", bool_text(acb.has_embedded_awb())));
    if (auto embedded = acb.embedded_awb()) {
        doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Embedded AWB bytes", number(embedded->size())));
    }
    if (auto awb_path = acb.companion_awb_path()) {
        doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Companion AWB", generic_path(*awb_path)));
    }
    uint16_t associated_awb_subkey = 0;
    if (auto awb = acb.load_awb()) {
        doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Associated AWB files", number(awb->file_count())));
        doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Associated AWB alignment", number(awb->alignment())));
        doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Associated AWB subkey", number(awb->subkey())));
        associated_awb_subkey = awb->subkey();
    } else {
        doc.info.push_back(translated_info_row("Acb.AcbBrowse", "Associated AWB", awb.error()));
    }
    doc.entry_columns = {cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Name"), "Codec"};
    doc.entry_column_types = {"name", "type"};

    doc.entries.reserve(acb.waveform_count());
    for (uint32_t i = 0; i < acb.waveform_count(); ++i) {
        const auto& wave = acb.waveform(i);
        const auto resolved_codec = acb.waveform_codec(i);
        const auto codec = std::string(cricodecs::awb::entry_codec_name(
            resolved_codec.value_or(cricodecs::awb::EntryCodec::Unknown)));
        auto entry = source_entry({
            acb.waveform_filename(i),
            codec,
            indexed_label("waveform", i),
            "memory " + number(wave.memory_awb_id) + cristudio::i18n::translate_utf8("Acb.AcbBrowse", ", stream ") + number(wave.stream_awb_id),
            bool_text(wave.loop_flag)
        }, path, "ACB", i);
        entry.hca_subkey = associated_awb_subkey;
        entry.cells = {
            entry.name,
            entry.type
        };
        doc.entries.push_back(std::move(entry));
    }
    doc.acb_cue_sheet = std::make_shared<const CueSheetView>(
        build_cue_sheet_view(path, acb));
    return doc;
}

} // namespace cristudio::modules::acb
