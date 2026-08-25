#include "shared/i18n.hpp"
#include "modules/hca/hca_browse.hpp"

#include "modules/hca/hca_common.hpp"
#include "shared/document_helpers.hpp"

namespace cristudio::modules::hca {

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::hca::Hca& hca) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Hca.HcaBrowse", "HCA audio"));
    const auto& header = hca.header();
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Version", hex_number(header.file.version)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Header size", number(header.file.header_size)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Channels", number(header.fmt.channel_count)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Sample rate", number(header.fmt.sample_rate)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Frames", number(header.fmt.frame_count)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Samples", number(header.sample_count())));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Encoder delay", number(header.fmt.encoder_delay)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Encoder padding", number(header.fmt.encoder_padding)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Codec type", codec_type_name(header.codec.type())));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Frame size", number(header.codec.frame_size)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Resolution", number(header.codec.min_resolution) + "-" + number(header.codec.max_resolution)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Track count", number(header.codec.track_count)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Channel config", number(header.codec.channel_config)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Bands", cristudio::i18n::translate_utf8("Hca.HcaBrowse", "total ") + number(header.codec.total_band_count) +
        cristudio::i18n::translate_utf8("Hca.HcaBrowse", ", base ") + number(header.codec.base_band_count) +
        cristudio::i18n::translate_utf8("Hca.HcaBrowse", ", stereo ") + number(header.codec.stereo_band_count) +
        cristudio::i18n::translate_utf8("Hca.HcaBrowse", ", hfr ") + number(header.codec.bands_per_hfr_group) +
        " x " + number(header.codec.hfr_group_count)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "MS stereo", bool_text(header.codec.uses_ms_stereo())));
    doc.info.push_back({"VBR", header.vbr.enabled()
        ? cristudio::i18n::translate_utf8("Hca.HcaBrowse", "max frame ") + number(header.vbr.max_frame_size) + cristudio::i18n::translate_utf8("Hca.HcaBrowse", ", noise ") + number(header.vbr.noise_level)
        : cristudio::i18n::translate_utf8("Hca.HcaBrowse", "no")});
    doc.info.push_back({"ATH", cristudio::i18n::translate_utf8("Hca.HcaBrowse", "type ") + number(header.ath.type) + cristudio::i18n::translate_utf8("Hca.HcaBrowse", ", curve ") + bool_text(header.ath.uses_curve())});
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Cipher type", number(header.cipher.type), "Cipher type"));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Loop", header.loop.enabled()
        ? cristudio::i18n::translate_utf8("Hca.HcaBrowse", "frames ") + number(header.loop.start_frame) + "-" + number(header.loop.end_frame) +
            cristudio::i18n::translate_utf8("Hca.HcaBrowse", ", delay ") + number(header.loop.start_delay) +
            cristudio::i18n::translate_utf8("Hca.HcaBrowse", ", padding ") + number(header.loop.end_padding)
        : cristudio::i18n::translate_utf8("Hca.HcaBrowse", "no")));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "RVA volume", float_text(header.rva.volume)));
    doc.info.push_back(translated_info_row("Hca.HcaBrowse", "Comment length", number(header.comment.length)));
    return doc;
}

} // namespace cristudio::modules::hca
