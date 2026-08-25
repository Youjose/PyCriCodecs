#include "shared/i18n.hpp"
#include "modules/aix/aix_browse.hpp"

#include "shared/document_helpers.hpp"

#include <algorithm>
#include <utility>

namespace cristudio::modules::aix {
LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::aix::Aix& aix) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Aix.AixBrowse", "AIX audio container"));
    doc.info.push_back(translated_info_row("Aix.AixBrowse", "Segments", number(aix.segments().size())));
    doc.info.push_back(translated_info_row("Aix.AixBrowse", "Layers", number(aix.layers().size())));
    doc.info.push_back(translated_info_row("Aix.AixBrowse", "Samples", number(aix.total_sample_count())));
    doc.info.push_back(translated_info_row("Aix.AixBrowse", "Inferred loop", bool_text(aix.inferred_loop().has_value())));

    const auto layer_count = std::max<size_t>(aix.layers().size(), 1);
    for (size_t segment_index = 0; segment_index < aix.segments().size(); ++segment_index) {
        const auto& segment = aix.segments()[segment_index];
        if (layer_count <= 1) {
            const auto detail = aix.layers().empty()
                ? "samples " + number(segment.sample_count) + ", " + number(segment.sample_rate) + " Hz"
                : "samples " + number(segment.sample_count) + ", " +
                    number(aix.layers().front().sample_rate == 0 ? static_cast<uint32_t>(segment.sample_rate) : aix.layers().front().sample_rate) +
                    " Hz, " + number(aix.layers().front().channel_count) + " ch";
            doc.entries.push_back(source_entry({
                "segment " + number(segment_index),
                "ADX",
                byte_count(segment.size),
                number(segment.offset),
                detail
            }, path, "AIX", static_cast<uint32_t>(segment_index * layer_count)));
            continue;
        }

        for (size_t layer_index = 0; layer_index < aix.layers().size(); ++layer_index) {
            const auto& layer = aix.layers()[layer_index];
            doc.entries.push_back(source_entry({
                "segment " + number(segment_index) + "/layer " + number(layer_index),
                "ADX",
                byte_count(segment.size),
                number(segment.offset),
                "samples " + number(segment.sample_count) + ", " +
                    number(layer.sample_rate == 0 ? static_cast<uint32_t>(segment.sample_rate) : layer.sample_rate) +
                    " Hz, " + number(layer.channel_count) + " ch"
            }, path, "AIX", static_cast<uint32_t>(segment_index * layer_count + layer_index)));
        }
    }
    return doc;
}

} // namespace cristudio::modules::aix
