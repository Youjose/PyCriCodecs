#include "usm_container.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>

namespace cricodecs::usm {
namespace {

constexpr size_t sbt_record_header_size = 0x14;

[[nodiscard]] std::string format_srt_cues(std::span<const UsmSubtitleCue> cues);

[[nodiscard]] std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

[[nodiscard]] std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t offset = 0;
    while (offset <= text.size()) {
        const auto newline = text.find('\n', offset);
        auto line = newline == std::string_view::npos ? text.substr(offset) : text.substr(offset, newline - offset);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (newline == std::string_view::npos) {
            break;
        }
        offset = newline + 1;
    }
    return lines;
}

template <class T>
[[nodiscard]] std::expected<T, std::string> parse_integer(std::string_view text, std::string_view label) {
    text = trim(text);
    if (text.empty()) {
        return std::unexpected("USM subtitle parse failed: failed to parse " + std::string(label));
    }
    T value{};
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::unexpected("USM subtitle parse failed: invalid " + std::string(label));
    }
    return value;
}

[[nodiscard]] std::expected<uint32_t, std::string> parse_clock_time(
    std::string_view text,
    char fraction_separator,
    uint32_t fraction_limit,
    uint32_t fraction_milliseconds,
    std::string_view format,
    std::string_view fraction_name
) {
    text = trim(text);
    const auto first_colon = text.find(':');
    const auto second_colon = first_colon == std::string_view::npos
        ? std::string_view::npos
        : text.find(':', first_colon + 1);
    const auto fraction = second_colon == std::string_view::npos
        ? std::string_view::npos
        : text.find(fraction_separator, second_colon + 1);
    if (first_colon == std::string_view::npos || second_colon == std::string_view::npos ||
        fraction == std::string_view::npos) {
        return std::unexpected("USM subtitle parse failed: invalid " + std::string(format) + " timestamp");
    }

    auto hours = parse_integer<uint32_t>(text.substr(0, first_colon), std::string(format) + " hours");
    auto minutes = parse_integer<uint32_t>(
        text.substr(first_colon + 1, second_colon - first_colon - 1),
        std::string(format) + " minutes");
    auto seconds = parse_integer<uint32_t>(
        text.substr(second_colon + 1, fraction - second_colon - 1),
        std::string(format) + " seconds");
    auto fractions = parse_integer<uint32_t>(
        text.substr(fraction + 1),
        std::string(format) + " " + std::string(fraction_name));
    if (!hours || !minutes || !seconds || !fractions) {
        return std::unexpected(
            !hours ? hours.error() : !minutes ? minutes.error() : !seconds ? seconds.error() : fractions.error()
        );
    }
    if (*minutes >= 60 || *seconds >= 60 || *fractions >= fraction_limit) {
        return std::unexpected(
            "USM subtitle parse failed: " + std::string(format) + " timestamp component is out of range"
        );
    }

    const uint64_t total = ((static_cast<uint64_t>(*hours) * 60 + *minutes) * 60 + *seconds) * 1000 +
        static_cast<uint64_t>(*fractions) * fraction_milliseconds;
    if (total > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("USM subtitle parse failed: " + std::string(format) + " timestamp is too large");
    }
    return static_cast<uint32_t>(total);
}

[[nodiscard]] std::expected<uint32_t, std::string> milliseconds_to_ticks(
    uint64_t milliseconds,
    uint32_t time_unit
) {
    if (time_unit == 0) {
        return std::unexpected("USM subtitle parse failed: zero time unit");
    }
    const uint64_t ticks = (milliseconds * time_unit + 500) / 1000;
    if (ticks > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("USM subtitle parse failed: timestamp exceeds SBT range");
    }
    return static_cast<uint32_t>(ticks);
}

[[nodiscard]] std::expected<uint32_t, std::string> parse_srt_time(std::string_view text) {
    return parse_clock_time(text, ',', 1000, 1, "SRT", "milliseconds");
}

[[nodiscard]] std::expected<uint32_t, std::string> parse_ass_time(std::string_view text) {
    return parse_clock_time(text, '.', 100, 10, "ASS", "centiseconds");
}

[[nodiscard]] std::string format_clock_time(
    uint32_t ticks,
    uint32_t time_unit,
    uint32_t fractions_per_second,
    char separator,
    int fraction_width,
    bool pad_hours
) {
    const auto total_fractions =
        (static_cast<uint64_t>(ticks) * fractions_per_second + time_unit / 2u) / time_unit;
    const auto fraction = total_fractions % fractions_per_second;
    const auto total_seconds = total_fractions / fractions_per_second;
    const auto seconds = total_seconds % 60;
    const auto total_minutes = total_seconds / 60;
    const auto minutes = total_minutes % 60;
    const auto hours = total_minutes / 60;

    std::ostringstream out;
    out.fill('0');
    if (pad_hours) {
        out.width(2);
    }
    out << hours << ':';
    out.width(2);
    out << minutes << ':';
    out.width(2);
    out << seconds << separator;
    out.width(fraction_width);
    out << fraction;
    return out.str();
}

[[nodiscard]] std::string format_srt_time(uint32_t ticks, uint32_t time_unit) {
    return format_clock_time(ticks, time_unit, 1000, ',', 3, true);
}

[[nodiscard]] std::string format_ass_time(uint32_t ticks, uint32_t time_unit) {
    return format_clock_time(ticks, time_unit, 100, '.', 2, false);
}

[[nodiscard]] std::string escape_ass_text(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            escaped += "\\N";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

[[nodiscard]] std::string unescape_ass_text(std::string_view text) {
    std::string unescaped;
    unescaped.reserve(text.size());
    for (size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\\' && index + 1 < text.size() && (text[index + 1] == 'N' || text[index + 1] == 'n')) {
            unescaped.push_back('\n');
            ++index;
        } else {
            unescaped.push_back(text[index]);
        }
    }
    return unescaped;
}

[[nodiscard]] uint32_t parse_ass_language_id(std::string_view name, uint32_t fallback) {
    name = trim(name);
    constexpr std::string_view prefix = "lang";
    if (!name.starts_with(prefix)) {
        return fallback;
    }
    name.remove_prefix(prefix.size());
    auto parsed = parse_integer<uint32_t>(name, "ASS language id");
    return parsed ? *parsed : fallback;
}

[[nodiscard]] std::expected<std::vector<UsmSubtitleCue>, std::string> srt_to_cues(
    std::string_view text,
    uint32_t language_id,
    uint32_t time_unit
) {
    std::vector<UsmSubtitleCue> cues;
    auto lines = split_lines(text);
    size_t index = 0;
    while (index < lines.size()) {
        while (index < lines.size() && trim(lines[index]).empty()) {
            ++index;
        }
        if (index >= lines.size()) {
            break;
        }

        auto cue_number = parse_integer<uint32_t>(lines[index], "SRT cue number");
        if (!cue_number) {
            return std::unexpected(cue_number.error());
        }
        ++index;
        if (index >= lines.size()) {
            return std::unexpected("USM subtitle parse failed: missing SRT timing line");
        }

        const auto arrow = lines[index].find("-->");
        if (arrow == std::string_view::npos) {
            return std::unexpected("USM subtitle parse failed: invalid SRT timing line");
        }
        auto start_ms = parse_srt_time(lines[index].substr(0, arrow));
        auto end_ms = parse_srt_time(lines[index].substr(arrow + 3));
        if (!start_ms || !end_ms) {
            return std::unexpected(start_ms ? end_ms.error() : start_ms.error());
        }
        if (*end_ms < *start_ms) {
            return std::unexpected("USM subtitle parse failed: SRT cue ends before it starts");
        }
        ++index;

        std::string cue_text;
        while (index < lines.size() && !trim(lines[index]).empty()) {
            if (!cue_text.empty()) {
                cue_text.push_back('\n');
            }
            cue_text += lines[index];
            ++index;
        }

        auto start_ticks = milliseconds_to_ticks(*start_ms, time_unit);
        auto end_ticks = milliseconds_to_ticks(*end_ms, time_unit);
        if (!start_ticks || !end_ticks) {
            return std::unexpected(start_ticks ? end_ticks.error() : start_ticks.error());
        }
        cues.push_back(UsmSubtitleCue{
            .language_id = language_id,
            .time_unit = time_unit,
            .start_time = *start_ticks,
            .duration = *end_ticks - *start_ticks,
            .text = std::move(cue_text),
        });
    }
    return cues;
}

[[nodiscard]] std::vector<std::string_view> split_ass_dialogue(std::string_view line) {
    std::vector<std::string_view> fields;
    fields.reserve(10);
    size_t offset = 0;
    for (int field = 0; field < 9; ++field) {
        const auto comma = line.find(',', offset);
        if (comma == std::string_view::npos) {
            fields.push_back(line.substr(offset));
            return fields;
        }
        fields.push_back(line.substr(offset, comma - offset));
        offset = comma + 1;
    }
    fields.push_back(line.substr(offset));
    return fields;
}

[[nodiscard]] std::expected<std::vector<UsmSubtitleCue>, std::string> ass_to_cues(
    std::string_view text,
    uint32_t language_id,
    uint32_t time_unit
) {
    std::vector<UsmSubtitleCue> cues;
    for (auto line : split_lines(text)) {
        line = trim(line);
        constexpr std::string_view prefix = "Dialogue:";
        if (!line.starts_with(prefix)) {
            continue;
        }
        line.remove_prefix(prefix.size());
        line = trim(line);
        auto fields = split_ass_dialogue(line);
        if (fields.size() < 10) {
            return std::unexpected("USM subtitle parse failed: malformed ASS Dialogue line");
        }

        auto start_ms = parse_ass_time(fields[1]);
        auto end_ms = parse_ass_time(fields[2]);
        if (!start_ms || !end_ms) {
            return std::unexpected(start_ms ? end_ms.error() : start_ms.error());
        }
        if (*end_ms < *start_ms) {
            return std::unexpected("USM subtitle parse failed: ASS cue ends before it starts");
        }
        auto start_ticks = milliseconds_to_ticks(*start_ms, time_unit);
        auto end_ticks = milliseconds_to_ticks(*end_ms, time_unit);
        if (!start_ticks || !end_ticks) {
            return std::unexpected(start_ticks ? end_ticks.error() : start_ticks.error());
        }
        cues.push_back(UsmSubtitleCue{
            .language_id = parse_ass_language_id(fields[4], language_id),
            .time_unit = time_unit,
            .start_time = *start_ticks,
            .duration = *end_ticks - *start_ticks,
            .text = unescape_ass_text(fields[9]),
        });
    }

    if (cues.empty()) {
        return std::unexpected("USM subtitle parse failed: ASS text has no Dialogue rows");
    }
    return cues;
}

std::string format_srt_cues(std::span<const UsmSubtitleCue> cues) {
    std::ostringstream out;
    uint32_t index = 1;
    for (const auto& cue : cues) {
        out << index++ << '\n'
            << format_srt_time(cue.start_time, cue.time_unit)
            << " --> "
            << format_srt_time(cue.end_time(), cue.time_unit)
            << '\n'
            << cue.text
            << "\n\n";
    }
    return out.str();
}

} // namespace

std::expected<std::vector<UsmSubtitleCue>, std::string> parse_sbt_subtitles(std::span<const uint8_t> data) {
    std::vector<UsmSubtitleCue> cues;
    size_t offset = 0;
    while (offset < data.size()) {
        if (data.size() - offset < sbt_record_header_size) {
            return std::unexpected("USM SBT parse failed: record header is truncated");
        }

        const auto* record = data.data() + offset;
        const auto language_id = io::read_le<uint32_t>(record + 0x00);
        const auto time_unit = io::read_le<uint32_t>(record + 0x04);
        const auto start_time = io::read_le<uint32_t>(record + 0x08);
        const auto duration = io::read_le<uint32_t>(record + 0x0C);
        const auto text_size = io::read_le<uint32_t>(record + 0x10);
        offset += sbt_record_header_size;

        if (time_unit == 0) {
            return std::unexpected("USM SBT parse failed: record has zero time unit");
        }
        if (data.size() - offset < text_size) {
            return std::unexpected("USM SBT parse failed: record text is truncated");
        }

        auto text_bytes = data.subspan(offset, text_size);
        uint32_t terminator_size = 0;
        while (!text_bytes.empty() && text_bytes.back() == 0) {
            text_bytes = text_bytes.first(text_bytes.size() - 1);
            ++terminator_size;
        }
        cues.push_back(UsmSubtitleCue{
            .language_id = language_id,
            .time_unit = time_unit,
            .start_time = start_time,
            .duration = duration,
            .text = std::string(text_bytes.begin(), text_bytes.end()),
            .terminator_size = terminator_size,
        });
        offset += text_size;
    }
    return cues;
}

std::expected<std::vector<uint8_t>, std::string> build_sbt_subtitles(std::span<const UsmSubtitleCue> cues) {
    std::vector<uint8_t> data;
    for (const auto& cue : cues) {
        if (cue.time_unit == 0) {
            return std::unexpected("USM SBT build failed: cue has zero time unit");
        }
        if (cue.terminator_size > 2) {
            return std::unexpected("USM SBT build failed: cue terminator size is unsupported");
        }
        if (cue.text.size() > std::numeric_limits<uint32_t>::max() - cue.terminator_size) {
            return std::unexpected("USM SBT build failed: cue text is too large");
        }
        if (cue.duration > std::numeric_limits<uint32_t>::max() - cue.start_time) {
            return std::unexpected("USM SBT build failed: cue end time exceeds SBT range");
        }

        const auto offset = data.size();
        const auto text_size = cue.text.size() + cue.terminator_size;
        data.resize(data.size() + sbt_record_header_size + text_size);
        io::write_le<uint32_t>(data.data() + offset + 0x00, cue.language_id);
        io::write_le<uint32_t>(data.data() + offset + 0x04, cue.time_unit);
        io::write_le<uint32_t>(data.data() + offset + 0x08, cue.start_time);
        io::write_le<uint32_t>(data.data() + offset + 0x0C, cue.duration);
        io::write_le<uint32_t>(data.data() + offset + 0x10, static_cast<uint32_t>(text_size));
        std::ranges::copy(cue.text, data.begin() + static_cast<std::ptrdiff_t>(offset + sbt_record_header_size));
    }
    return data;
}

std::expected<std::string, std::string> sbt_to_subtitle_source_text(std::span<const uint8_t> data) {
    auto cues = parse_sbt_subtitles(data);
    if (!cues) {
        return std::unexpected(cues.error());
    }
    if (cues->empty()) {
        return std::unexpected("USM SBT source conversion failed: no subtitle cues");
    }

    std::ostringstream out;
    const bool include_language_id = std::ranges::any_of(*cues, [](const UsmSubtitleCue& cue) {
        return cue.language_id != 0;
    });

    out << cues->front().time_unit;
    for (const auto& cue : *cues) {
        if (cue.time_unit != cues->front().time_unit) {
            return std::unexpected("USM SBT source conversion failed: mixed time units are unsupported");
        }
        out << '\n';
        if (include_language_id) {
            out << cue.language_id << ", ";
        }
        out << cue.start_time << ", " << cue.end_time() << ", " << cue.text;
    }
    return out.str();
}

std::expected<std::vector<uint8_t>, std::string> subtitle_source_text_to_sbt(
    std::string_view text,
    uint32_t language_id
) {
    const auto lines = split_lines(text);
    if (lines.empty()) {
        return std::unexpected("USM subtitle source parse failed: missing time unit");
    }
    auto time_unit = parse_integer<uint32_t>(lines.front(), "subtitle source time unit");
    if (!time_unit || *time_unit == 0) {
        return std::unexpected(time_unit ? "USM subtitle source parse failed: zero time unit" : time_unit.error());
    }

    std::vector<UsmSubtitleCue> cues;
    for (size_t line_index = 1; line_index < lines.size(); ++line_index) {
        auto line = lines[line_index];
        if (trim(line).empty()) {
            continue;
        }
        const auto line_offset = static_cast<size_t>(line.data() - text.data());
        const auto line_end = line_offset + line.size();
        const bool newline_terminated =
            line_end < text.size() &&
            (text[line_end] == '\n' ||
             (text[line_end] == '\r' && line_end + 1u < text.size() && text[line_end + 1u] == '\n'));
        const auto first_comma = line.find(',');
        const auto second_comma = first_comma == std::string_view::npos
            ? std::string_view::npos
            : line.find(',', first_comma + 1);
        if (first_comma == std::string_view::npos || second_comma == std::string_view::npos) {
            return std::unexpected("USM subtitle source parse failed: malformed cue line");
        }
        const auto third_comma = line.find(',', second_comma + 1);
        const bool has_language_id = third_comma != std::string_view::npos;
        uint32_t cue_language_id = language_id;
        std::string_view start_text = line.substr(0, first_comma);
        std::string_view end_text = line.substr(first_comma + 1, second_comma - first_comma - 1);
        std::string_view cue_text = line.substr(second_comma + 1);
        if (has_language_id) {
            auto parsed_language_id = parse_integer<uint32_t>(line.substr(0, first_comma), "subtitle source language id");
            if (!parsed_language_id) {
                return std::unexpected(parsed_language_id.error());
            }
            cue_language_id = *parsed_language_id;
            start_text = line.substr(first_comma + 1, second_comma - first_comma - 1);
            end_text = line.substr(second_comma + 1, third_comma - second_comma - 1);
            cue_text = line.substr(third_comma + 1);
        }

        auto start = parse_integer<uint32_t>(start_text, "subtitle source start time");
        auto end = parse_integer<uint32_t>(end_text, "subtitle source end time");
        if (!start || !end) {
            return std::unexpected(start ? end.error() : start.error());
        }
        if (*end < *start) {
            return std::unexpected("USM subtitle source parse failed: cue ends before it starts");
        }
        cues.push_back(UsmSubtitleCue{
            .language_id = cue_language_id,
            .time_unit = *time_unit,
            .start_time = *start,
            .duration = *end - *start,
            .text = std::string(trim(cue_text)),
            .terminator_size = newline_terminated ? 1u : 0u,
        });
    }

    return build_sbt_subtitles(cues);
}

std::expected<std::string, std::string> sbt_to_srt(std::span<const uint8_t> data) {
    auto cues = parse_sbt_subtitles(data);
    if (!cues) {
        return std::unexpected(cues.error());
    }

    return format_srt_cues(*cues);
}

std::expected<std::flat_map<uint32_t, std::string>, std::string> sbt_to_srt_tracks(std::span<const uint8_t> data) {
    auto cues = parse_sbt_subtitles(data);
    if (!cues) {
        return std::unexpected(cues.error());
    }

    std::flat_map<uint32_t, std::vector<UsmSubtitleCue>> grouped_cues;
    for (const auto& cue : *cues) {
        grouped_cues[cue.language_id].push_back(cue);
    }

    std::flat_map<uint32_t, std::string> tracks;
    for (const auto& [language_id, language_cues] : grouped_cues) {
        tracks.emplace(language_id, format_srt_cues(language_cues));
    }
    return tracks;
}

std::expected<std::vector<uint8_t>, std::string> srt_to_sbt(
    std::string_view text,
    uint32_t language_id,
    uint32_t time_unit
) {
    auto cues = srt_to_cues(text, language_id, time_unit);
    if (!cues) {
        return std::unexpected(cues.error());
    }
    return build_sbt_subtitles(*cues);
}

std::expected<std::string, std::string> sbt_to_ass(std::span<const uint8_t> data, std::string_view title) {
    auto cues = parse_sbt_subtitles(data);
    if (!cues) {
        return std::unexpected(cues.error());
    }

    std::ostringstream out;
    out << "[Script Info]\n"
        << "Title: " << title << "\n"
        << "ScriptType: v4.00+\n"
        << "\n[V4+ Styles]\n"
        << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
           "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
           "Alignment, MarginL, MarginR, MarginV, Encoding\n"
        << "Style: Default,Arial,36,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,"
           "0,0,0,0,100,100,0,0,1,2,0,2,20,20,20,1\n"
        << "\n[Events]\n"
        << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

    for (const auto& cue : *cues) {
        out << "Dialogue: 0,"
            << format_ass_time(cue.start_time, cue.time_unit)
            << ','
            << format_ass_time(cue.end_time(), cue.time_unit)
            << ",Default,lang" << cue.language_id << ",0,0,0,,"
            << escape_ass_text(cue.text)
            << '\n';
    }
    return out.str();
}

std::expected<std::vector<uint8_t>, std::string> ass_to_sbt(
    std::string_view text,
    uint32_t language_id,
    uint32_t time_unit
) {
    auto cues = ass_to_cues(text, language_id, time_unit);
    if (!cues) {
        return std::unexpected(cues.error());
    }
    return build_sbt_subtitles(*cues);
}

} // namespace cricodecs::usm
