#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cristudio {

namespace modules::acb {
struct CueSheetView;
}

struct DecryptionKeys {
    enum class AdxMode : uint8_t {
        None,
        Type8String,
        Type9Number,
        AhxTriplet
    };

    AdxMode adx_mode = AdxMode::None;
    std::string adx_type8_key;
    uint64_t adx_type9_key = 0;
    uint16_t adx_subkey = 0;
    uint16_t ahx_start = 0;
    uint16_t ahx_mult = 0;
    uint16_t ahx_add = 0;
    bool has_cri_key = false;
    uint64_t cri_key = 0;
    uint16_t hca_subkey = 0;
};

struct InfoRow {
    enum class TranslationAffix : uint8_t {
        None,
        Prefix,
        Suffix,
    };

    std::string name;
    std::string value;
    std::string id;
    std::string value_id;
    const char* name_translation_context = nullptr;
    const char* name_translation_source = nullptr;
    const char* value_translation_context = nullptr;
    const char* value_translation_source = nullptr;
    std::string name_translation_affix;
    TranslationAffix name_translation_affix_position = TranslationAffix::None;
};

struct EntrySummary {
    std::string name;
    std::string type;
    std::string size;
    std::string offset;
    std::string detail;
    std::vector<std::string> cells;
    std::vector<uint32_t> cell_source_indices;
    std::vector<EntrySummary> inspector_entries;
    std::vector<uint8_t> thumbnail_bytes;
    std::filesystem::path source_path;
    std::string source_format;
    uint32_t source_index = 0;
    bool has_source = false;
    bool has_cell_sources = false;
    std::string nested_source_format;
    uint32_t nested_source_index = 0;
    bool has_nested_source = false;
    uint32_t video_frame_rate_n = 0;
    uint32_t video_frame_rate_d = 0;
    uint32_t video_total_frames = 0;
    uint16_t hca_subkey = 0;
};

struct LoadedDocument {
    std::filesystem::path path;
    std::string display_name;
    std::string format;
    std::string loader_tag;
    uintmax_t file_size = 0;
    std::vector<InfoRow> info;
    std::vector<std::string> entry_columns;
    std::vector<std::string> entry_column_types;
    std::vector<EntrySummary> entries;
    std::shared_ptr<const modules::acb::CueSheetView> acb_cue_sheet;
    bool summary_loaded = true;
};

[[nodiscard]] inline std::string_view document_format_id(const LoadedDocument& document) noexcept {
    return document.loader_tag.empty() ? std::string_view(document.format) : std::string_view(document.loader_tag);
}

[[nodiscard]] inline std::string_view info_row_id(const InfoRow& row) noexcept {
    return row.id.empty() ? std::string_view(row.name) : std::string_view(row.id);
}

[[nodiscard]] inline std::string_view info_row_value_id(const InfoRow& row) noexcept {
    return row.value_id.empty() ? std::string_view(row.value) : std::string_view(row.value_id);
}

struct AudioLoop {
    std::string name;
    uint64_t start_sample = 0;
    uint64_t end_sample = 0;
};

struct AudioPreview {
    std::filesystem::path playable_path;
    std::vector<uint8_t> wav_bytes;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint64_t sample_count = 0;
    std::string format;
    std::string note;
    std::vector<AudioLoop> loops;
};

struct VideoPreview {
    std::filesystem::path playable_path;
    std::filesystem::path temporary_directory;
    std::vector<uint8_t> video_bytes;
    std::string file_suffix;
    std::string ffmpeg_input_format;
    std::string format;
    std::string note;
    uint32_t frame_rate_n = 0;
    uint32_t frame_rate_d = 0;
    uint64_t duration_ms = 0;
    bool remux_for_playback = false;
};

struct MuxAudioChoice {
    std::string name;
    std::string type;
    std::string detail;
    uint32_t source_index = 0;
};

struct MuxSubtitleChoice {
    std::string name;
    std::string detail;
    uint32_t source_index = 0;
    uint32_t language_id = 0;
    std::string srt_text;
};

struct MuxPreview {
    std::filesystem::path playable_path;
    std::filesystem::path temporary_directory;
    std::vector<uint8_t> video_bytes;
    std::string video_suffix;
    std::string ffmpeg_input_format;
    std::string format;
    std::string note;
    uint32_t frame_rate_n = 0;
    uint32_t frame_rate_d = 0;
    uint64_t duration_ms = 0;
    std::vector<MuxAudioChoice> audio_choices;
    int selected_audio = -1;
    std::vector<uint8_t> audio_wav_bytes;
    std::string audio_label;
    std::vector<MuxSubtitleChoice> subtitle_choices;
    int selected_subtitle = -1;
};

struct EmbeddedPreview {
    std::optional<LoadedDocument> document;
    std::optional<AudioPreview> audio;
    std::optional<VideoPreview> video;
    std::string message;
    std::vector<uint8_t> raw_preview_bytes;
    uint64_t raw_total_size = 0;
    std::vector<uint8_t> preview_bytes;
};

enum class ExtractionMode {
    Decoded,
    Raw
};

struct ExtractionEvent {
    size_t processed_delta = 0;
    size_t extracted_delta = 0;
    size_t failed_delta = 0;
    std::string message;
};

struct ExtractionOptions {
    bool include_mux_outputs = true;
    bool render_acb_cues = false;
    int mux_audio_choice = 0;
    std::filesystem::path ffmpeg_path;
    std::stop_token stop_token;
    std::function<void(const ExtractionEvent&)> event_callback;
};

struct ExtractionTarget {
    enum class Kind : uint8_t {
        Document,
        Entry,
        AcbCue,
    };

    Kind kind = Kind::Document;
    LoadedDocument document;
    EntrySummary entry;
    std::filesystem::path acb_path;
    uint32_t acb_cue_index = 0;
    std::string acb_plan_signature;
    std::string acb_output_name;
    bool acb_include_empty_holds = false;
};

struct ExtractionReport {
    size_t total = 0;
    size_t extracted = 0;
    size_t failed = 0;
    bool canceled = false;
    bool messages_logged_live = false;
    std::optional<std::filesystem::path> diagnostic_path = std::nullopt;
    std::vector<std::filesystem::path> output_paths;
    std::vector<std::string> messages;
};
} // namespace cristudio
