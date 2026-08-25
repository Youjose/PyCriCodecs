#include "shared/i18n.hpp"
#include "modules/aax/aax_browse.hpp"

#include "shared/document_helpers.hpp"

#include <optional>
#include <variant>

namespace cristudio::modules::aax {
namespace {

uint64_t aax_data_base(const cricodecs::aax::AaxContainer& aax) {
    return aax.table().data_offset();
}

std::optional<uint64_t> segment_offset(const cricodecs::aax::AaxContainer& aax, uint32_t row) {
    const auto data_col = aax.table().find_column("data");
    if (data_col < 0) {
        return std::nullopt;
    }
    auto value = aax.table().get_value(row, static_cast<uint32_t>(data_col));
    if (!value || !std::holds_alternative<cricodecs::utf::DataRef>(*value)) {
        return std::nullopt;
    }
    return aax_data_base(aax) + std::get<cricodecs::utf::DataRef>(*value).offset;
}

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::aax::AaxContainer& aax) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Aax.AaxBrowse", "AAX audio wrapper"));
    doc.info.push_back(translated_info_row("Aax.AaxBrowse", "Name", std::string(aax.name())));
    doc.info.push_back(translated_info_row("Aax.AaxBrowse", "Segments", number(aax.segment_count())));
    doc.info.push_back(translated_info_row("Aax.AaxBrowse", "Channels", number(aax.channels())));
    doc.info.push_back(translated_info_row("Aax.AaxBrowse", "Sample rate", number(aax.sample_rate())));
    doc.info.push_back(translated_info_row("Aax.AaxBrowse", "Samples", number(aax.sample_count())));
    doc.info.push_back(translated_info_row("Aax.AaxBrowse", "Loop segments", bool_text(aax.has_loop_segments())));
    doc.entry_columns = {"Segment", "Codec", "Bytes", cristudio::i18n::translate_utf8("Aax.AaxBrowse", "Samples"), "Loop"};
    doc.entry_column_types = {"name", "type", "size", "u32", "bool"};

    doc.entries.reserve(aax.segments().size());
    for (const auto& segment : aax.segments()) {
        auto entry = source_entry({
            "segment " + number(segment.row_index),
            "ADX",
            byte_count(segment.data_size),
            {},
            bool_text(segment.loop_segment)
        }, path, "AAX", segment.row_index);
        if (auto offset = segment_offset(aax, segment.row_index)) {
            entry.offset = number(*offset);
        }
        entry.cells = {
            entry.name,
            entry.type,
            entry.size,
            number(segment.sample_count),
            entry.detail
        };
        doc.entries.push_back(std::move(entry));
    }
    return doc;
}

} // namespace cristudio::modules::aax
