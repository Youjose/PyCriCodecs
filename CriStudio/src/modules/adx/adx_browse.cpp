#include "shared/i18n.hpp"
#include "modules/adx/adx_browse.hpp"

#include "shared/document_helpers.hpp"

#include <cstdint>
namespace cristudio::modules::adx {
LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::adx::Adx& adx) {
    auto doc = base_document(path, adx.is_ahx() ? cristudio::i18n::translate_utf8("Adx.AdxBrowse", "AHX audio") : cristudio::i18n::translate_utf8("Adx.AdxBrowse", "ADX audio"));
    const auto& header = adx.header();
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Signature", hex_number(header.signature)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Data offset", number(header.data_offset)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Encoding mode", number(header.encoding_mode)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Block size", number(header.block_size)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Bit depth", number(header.bit_depth)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Channels", number(header.channels)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Sample rate", number(header.sample_rate)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Samples", number(header.sample_count)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Highpass", number(header.highpass_freq)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Version", number(header.version)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Flags", hex_number(header.flags)));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Encrypted", bool_text(adx.is_encrypted()), "Encrypted"));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "AHX routed", bool_text(adx.is_ahx()), "AHX routed"));
    doc.info.push_back(translated_info_row("Adx.AdxBrowse", "Loop count", number(adx.loops().size())));
    for (const auto& loop : adx.loops()) {
        doc.info.push_back(translated_info_row_with_affix(
            "Adx.AdxBrowse",
            "Loop",
            " " + number(loop.index),
            InfoRow::TranslationAffix::Suffix,
            cristudio::i18n::translate_utf8("Adx.AdxBrowse", "type ") + number(loop.type) +
                cristudio::i18n::translate_utf8("Adx.AdxBrowse", ", samples ") + number(loop.start_sample) + "-" + number(loop.end_sample) +
                cristudio::i18n::translate_utf8("Adx.AdxBrowse", ", bytes ") + number(loop.start_byte) + "-" + number(loop.end_byte)
        ));
    }
    return doc;
}

} // namespace cristudio::modules::adx
