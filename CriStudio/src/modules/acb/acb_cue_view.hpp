#pragma once

#include "document/document_types.hpp"

#include "acb_container.hpp"
#include "acb_cue_renderer.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cristudio::modules::acb {

struct CueSelectorValueView {
    std::string name;
    std::string value;
};

struct CueChoiceView {
    cricodecs::acb::AcbCueChoiceDomain domain =
        cricodecs::acb::AcbCueChoiceDomain::sequence_track;
    uint32_t node_index = 0;
    uint32_t occurrence = 0;
    uint32_t option_index = 0;
    uint8_t mode = 0;
    std::string selector_name;
    std::string selector_value;
};

struct CueClipView {
    uint32_t waveform_index = 0;
    int64_t start_time_us = 0;
    std::optional<uint16_t> awb_wave_id;
    std::optional<uint32_t> awb_stream_index;
    std::string awb_bank;
};

struct CueBlockView {
    std::string name;
    uint64_t duration_us = 0;
    int32_t authored_loop_count = 0;
    uint32_t render_loop_count = 0;
    bool skipped_empty_hold = false;
    std::vector<CueClipView> clips;
};

struct CueCommandView {
    std::string table;
    uint32_t row_index = 0;
    uint32_t command_index = 0;
    uint16_t code = 0;
    std::string meaning;
    std::optional<uint16_t> target_type;
    std::optional<uint16_t> target_index;
    std::optional<uint16_t> argument_u16;
    std::optional<uint16_t> argument_u16_2;
    std::optional<int16_t> argument_i16;
};

struct CuePlanView {
    std::string semantic_signature;
    std::string output_name;
    uint32_t terminal_cue_index = 0;
    uint32_t terminal_cue_id = 0;
    std::string terminal_cue_name;
    std::vector<CueBlockView> blocks;
};

struct CueRouteView {
    uint32_t plan_index = 0;
    std::vector<uint32_t> action_cue_chain;
    std::vector<std::string> action_cue_names;
    std::vector<CueSelectorValueView> selectors;
    std::vector<CueChoiceView> choices;
};

struct CueView {
    uint32_t cue_index = 0;
    uint32_t cue_id = 0;
    uint16_t reference_type = 0;
    uint16_t reference_index = 0;
    std::string name;
    std::string reference_name;
    std::vector<CueRouteView> routes;
    std::vector<CueCommandView> commands;
    std::string status;
};

struct CueSheetView {
    std::vector<CuePlanView> plans;
    std::vector<CueView> cues;
    std::vector<EntrySummary> entries;
    std::vector<std::string> entry_columns;
    std::vector<std::string> entry_column_types;
};

[[nodiscard]] CueSheetView build_cue_sheet_view(
    const std::filesystem::path& path,
    const cricodecs::acb::AcbContainer& acb);

[[nodiscard]] std::expected<AudioPreview, std::string> render_cue_preview(
    const std::filesystem::path& path,
    uint32_t source_cue_index,
    std::string_view semantic_signature,
    const DecryptionKeys& keys,
    bool include_empty_holds = false);

[[nodiscard]] std::string cue_route_label(
    const CueSheetView& sheet,
    const CueView& cue,
    const CueRouteView& route,
    size_t route_index);

} // namespace cristudio::modules::acb
