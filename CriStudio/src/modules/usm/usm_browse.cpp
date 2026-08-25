#include "shared/i18n.hpp"
#include "modules/usm/usm_browse.hpp"

#include "path_text.hpp"
#include "shared/document_helpers.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace cristudio::modules::usm {
namespace {

std::string stream_family_type(cricodecs::usm::UsmChunkType type) {
    switch (type) {
    case cricodecs::usm::UsmChunkType::SFV:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "SFV video");
    case cricodecs::usm::UsmChunkType::ALP:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "ALP alpha video");
    case cricodecs::usm::UsmChunkType::SFA:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "SFA audio");
    case cricodecs::usm::UsmChunkType::AHX:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "AHX audio");
    case cricodecs::usm::UsmChunkType::SBT:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "SBT subtitles");
    case cricodecs::usm::UsmChunkType::CUE:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "CUE metadata");
    case cricodecs::usm::UsmChunkType::USR:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "USR user data");
    case cricodecs::usm::UsmChunkType::PST:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "PST picture metadata");
    case cricodecs::usm::UsmChunkType::ELM:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "ELM index metadata");
    case cricodecs::usm::UsmChunkType::STA:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "STA seek metadata");
    case cricodecs::usm::UsmChunkType::ATP:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "ATP picture metadata");
    case cricodecs::usm::UsmChunkType::SFSH:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "SFSH header");
    case cricodecs::usm::UsmChunkType::CRID:
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "CRID metadata");
    }
    return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "USM stream");
}

std::string audio_stream_type(const cricodecs::usm::UsmStreamInfo& stream) {
    if (stream.stream_id == cricodecs::usm::UsmChunkType::AHX) {
        return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "AHX audio");
    }
    if (stream.audio_codec) {
        const auto name = std::string(cricodecs::usm::audio_codec_name(*stream.audio_codec));
        if (name == "adx") {
            return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "ADX audio");
        }
        if (name == "hca") {
            return cristudio::i18n::translate_utf8("Usm.UsmBrowse", "HCA audio");
        }
        if (name != "unknown") {
            return name + " audio";
        }
    }
    return stream_family_type(stream.stream_id);
}

std::string video_stream_type(
    cricodecs::usm::UsmReader& usm,
    uint32_t stream_index,
    const cricodecs::usm::UsmStreamInfo& stream,
    const VideoFormatProbe& video_format_probe
) {
    if (video_format_probe) {
        if (auto payload = usm.extract_stream(stream_index)) {
            if (auto format = video_format_probe(*payload)) {
                return *format;
            }
        }
    }
    return stream_family_type(stream.stream_id);
}

std::string stream_type(
    cricodecs::usm::UsmReader& usm,
    uint32_t stream_index,
    const cricodecs::usm::UsmStreamInfo& stream,
    const VideoFormatProbe& video_format_probe
) {
    switch (stream.stream_id) {
    case cricodecs::usm::UsmChunkType::SFV:
    case cricodecs::usm::UsmChunkType::ALP:
        return video_stream_type(usm, stream_index, stream, video_format_probe);
    case cricodecs::usm::UsmChunkType::SFA:
    case cricodecs::usm::UsmChunkType::AHX:
        return audio_stream_type(stream);
    default:
        return stream_family_type(stream.stream_id);
    }
}

std::string language_list(const std::vector<cricodecs::usm::UsmSubtitleCue>& cues) {
    std::vector<uint32_t> languages;
    languages.reserve(cues.size());
    for (const auto& cue : cues) {
        languages.push_back(cue.language_id);
    }
    std::ranges::sort(languages);
    const auto last = std::ranges::unique(languages).begin();
    languages.erase(last, languages.end());

    std::ostringstream out;
    for (size_t i = 0; i < languages.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << languages[i];
    }
    return out.str();
}

std::string subtitle_time_text(const cricodecs::usm::UsmSubtitleCue& cue) {
    return number(cue.start_time) + "-" + number(cue.end_time()) + " / " + number(cue.time_unit);
}

} // namespace

std::optional<VideoMetadata> video_metadata(const cricodecs::usm::UsmReader& usm, uint32_t stream_index) {
    if (stream_index >= usm.streams().size()) {
        return std::nullopt;
    }

    const auto id = usm.streams()[stream_index].id();
    if (
        id.stream_id != cricodecs::usm::UsmChunkType::SFV &&
        id.stream_id != cricodecs::usm::UsmChunkType::ALP
    ) {
        return std::nullopt;
    }

    for (const auto& chunk : usm.chunks()) {
        if (
            !chunk.belongs_to(id) ||
            chunk.payload_type() != cricodecs::usm::UsmPayloadType::Header ||
            !chunk.is_utf_payload()
        ) {
            continue;
        }

        auto table = chunk.load_utf_payload();
        if (!table || table->table_name() != "VIDEO_HDRINFO" || table->row_count() == 0) {
            continue;
        }

        VideoMetadata metadata;
        if (auto frames = table->get<uint32_t>(0, "total_frames"); frames) {
            metadata.total_frames = *frames;
        }
        if (auto fps_n = table->get<uint32_t>(0, "framerate_n"); fps_n) {
            metadata.frame_rate_n = *fps_n;
        }
        if (auto fps_d = table->get<uint32_t>(0, "framerate_d"); fps_d) {
            metadata.frame_rate_d = *fps_d;
        }
        return metadata;
    }

    return std::nullopt;
}

std::optional<VideoMetadata> video_metadata_for_entry(const EntrySummary& entry) {
    if (entry.source_format != "USM" || entry.has_nested_source) {
        return std::nullopt;
    }

    cricodecs::usm::UsmReader usm;
    if (auto loaded = usm.load(entry.source_path); !loaded) {
        return std::nullopt;
    }
    return video_metadata(usm, entry.source_index);
}

LoadedDocument summarize(
    const std::filesystem::path& path,
    cricodecs::usm::UsmReader& usm,
    const VideoFormatProbe& video_format_probe,
    bool inspect_stream_payloads
) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Usm.UsmBrowse", "USM/SofDec stream"));
    const auto container_filename = archive_display_path(usm.container_filename());
    doc.info.push_back(translated_info_row("Usm.UsmBrowse", "Container filename", container_filename));
    doc.info.push_back(translated_info_row("Usm.UsmBrowse", "Streams", number(usm.streams().size())));
    doc.info.push_back(translated_info_row("Usm.UsmBrowse", "Chunks", number(usm.chunks().size())));
    doc.info.push_back({"SFSH", bool_text(usm.sfsh_header().has_value())});

    doc.entries.reserve(usm.streams().size());
    for (uint32_t i = 0; i < usm.streams().size(); ++i) {
        const auto& stream = usm.streams()[i];
        const auto stream_name = stream.filename.empty()
            ? archive_display_path(usm.describe_stream(stream.id()))
            : archive_display_path(stream.filename);
        auto entry = source_entry({
            stream_name,
            stream_type(usm, i, stream, video_format_probe),
            stream.filesize == 0 ? std::string{} : byte_count(stream.filesize),
            "channel " + number(stream.channel_no),
            "stream " + number(i) + cristudio::i18n::translate_utf8("Usm.UsmBrowse", ", avbps ") + number(stream.avbps)
        }, path, "USM", i);
        if (inspect_stream_payloads && stream.stream_id == cricodecs::usm::UsmChunkType::SBT) {
            if (auto payload = usm.extract_stream(i)) {
                if (auto cues = cricodecs::usm::parse_sbt_subtitles(*payload)) {
                    entry.detail += cristudio::i18n::translate_utf8("Usm.UsmBrowse", ", cues ") + number(cues->size());
                    if (!cues->empty()) {
                        entry.detail += cristudio::i18n::translate_utf8("Usm.UsmBrowse", ", languages ") + language_list(*cues);
                    }
                }
            }
        }
        if (auto metadata = video_metadata(usm, i)) {
            entry.video_frame_rate_n = metadata->frame_rate_n;
            entry.video_frame_rate_d = metadata->frame_rate_d;
            entry.video_total_frames = metadata->total_frames;
        }
        doc.entries.push_back(std::move(entry));
    }
    return doc;
}

LoadedDocument summarize_sbt_subtitles(
    const std::filesystem::path& path,
    std::span<const uint8_t> bytes,
    std::string& rejection_reason
) {
    auto cues = cricodecs::usm::parse_sbt_subtitles(bytes);
    if (!cues) {
        rejection_reason = cues.error();
        return {};
    }

    auto doc = base_document(path, cristudio::i18n::translate_utf8("Usm.UsmBrowse", "USM SBT subtitles"));
    doc.info.push_back(translated_info_row("Usm.UsmBrowse", "Cues", number(cues->size())));
    doc.info.push_back(translated_info_row("Usm.UsmBrowse", "Languages", language_list(*cues)));
    if (!cues->empty()) {
        doc.info.push_back(translated_info_row("Usm.UsmBrowse", "Time unit", number(cues->front().time_unit)));
        uint32_t total_time = 0;
        for (const auto& cue : *cues) {
            total_time = (std::max)(total_time, cue.end_time());
        }
        doc.info.push_back(translated_info_row("Usm.UsmBrowse", "Total time", number(total_time)));
    }
    doc.entry_columns = {"Language", "Time", "Text"};
    doc.entry_column_types = {"u32", "time", "string"};
    doc.entries.reserve(cues->size());
    for (size_t i = 0; i < cues->size(); ++i) {
        const auto& cue = (*cues)[i];
        EntrySummary entry;
        entry.name = cristudio::i18n::translate_utf8("Usm.UsmBrowse", "cue ") + number(i);
        entry.type = cristudio::i18n::translate_utf8("Usm.UsmBrowse", "subtitle");
        entry.size = number(cue.text.size()) + " chars";
        entry.offset = subtitle_time_text(cue);
        entry.detail = cue.text;
        entry.cells = {
            number(cue.language_id),
            subtitle_time_text(cue),
            cue.text
        };
        doc.entries.push_back(std::move(entry));
    }
    return doc;
}

} // namespace cristudio::modules::usm
