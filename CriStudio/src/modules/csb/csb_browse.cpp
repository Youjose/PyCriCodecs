#include "shared/i18n.hpp"
#include "modules/csb/csb_browse.hpp"

#include "path_text.hpp"
#include "shared/document_helpers.hpp"

#include <utility>

namespace cristudio::modules::csb {
LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::csb::CsbContainer& csb) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Csb.CsbBrowse", "CSB cue archive"));
    doc.info.push_back(translated_info_row("Csb.CsbBrowse", "Name", std::string(csb.name())));
    doc.info.push_back(translated_info_row("Csb.CsbBrowse", "Sections", number(csb.section_count())));
    doc.info.push_back(translated_info_row("Csb.CsbBrowse", "Elements", number(csb.element_count())));
    doc.info.push_back(translated_info_row("Csb.CsbBrowse", "Streams", number(csb.stream_count())));

    doc.entries.reserve(csb.stream_count());
    for (uint32_t i = 0; i < csb.stream_count(); ++i) {
        const auto& stream = csb.stream(i);
        doc.entries.push_back(source_entry({
            archive_display_path(stream.suggested_path().generic_string()),
            stream.wrapper_table_name.empty() ? std::string(cricodecs::csb::stream_file_extension(stream.format))
                                              : stream.wrapper_table_name,
            byte_count(stream.wrapper_size),
            indexed_label("stream", i),
            number(stream.channels) + " ch, " + number(stream.sample_rate) + " Hz"
        }, path, "CSB", i));
    }
    return doc;
}

} // namespace cristudio::modules::csb
