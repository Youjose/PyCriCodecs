#include "shared/i18n.hpp"
#include "modules/awb/awb_browse.hpp"

#include "shared/document_helpers.hpp"

#include <cstdint>
#include <utility>

namespace cristudio::modules::awb {
LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::awb::AwbContainer& awb) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Awb.AwbBrowse", "AWB audio bank"));
    doc.info.push_back(translated_info_row("Awb.AwbBrowse", "Entries", number(awb.file_count())));
    doc.info.push_back(translated_info_row("Awb.AwbBrowse", "Version", number(awb.version())));
    doc.info.push_back(translated_info_row("Awb.AwbBrowse", "Alignment", number(awb.alignment())));
    doc.info.push_back(translated_info_row("Awb.AwbBrowse", "ID size", number(awb.id_size())));
    doc.info.push_back(translated_info_row("Awb.AwbBrowse", "Offset size", number(awb.offset_size())));

    doc.entries.reserve(awb.entries().size());
    for (size_t i = 0; i < awb.entries().size(); ++i) {
        const auto& entry = awb.entries()[i];
        const auto codec = awb.entry_codec(static_cast<uint32_t>(i));
        const auto extension = codec
            ? cricodecs::awb::entry_codec_extension(*codec)
            : std::string_view{".bin"};
        auto summary = source_entry({
            "wave_" + number(entry.wave_id) + std::string(extension),
            codec ? std::string(cricodecs::awb::entry_codec_name(*codec)) : "audio",
            byte_count(entry.size),
            number(entry.offset),
            "index " + number(i)
        }, path, "AWB", static_cast<uint32_t>(i));
        summary.hca_subkey = awb.subkey();
        doc.entries.push_back(std::move(summary));
    }
    return doc;
}

} // namespace cristudio::modules::awb
