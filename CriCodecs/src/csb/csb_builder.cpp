/**
 * @file csb_builder.cpp
 * @brief CSB cue archive builder.
 *
 * Builder behavior follows vgmstream's CSB loader model where applicable, with
 * validation for the current C++23 rebuild path. Follow-up implementation by
 * Youjose.
 */

#include "csb_container.hpp"

#include <algorithm>
#include <array>
#include <optional>

#include "csb_format.hpp"
#include "../aax/aax_container.hpp"
#include "../adx/adx_codec.hpp"
#include "../hca/hca_codec.hpp"
#include "../utilities/flat_unordered_map.hpp"
#include "../utilities/io.hpp"
#include "../utilities/io_endian.hpp"

namespace cricodecs::csb {

namespace {

struct BuildStreamInfo {
    std::string name;
    std::string name_raw;
    uint8_t format = 0;
    std::string wrapper_table_name;
    uint8_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t sample_count = 0;
    bool looped = false;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> wrapper;
};

using detail::encode_cri_string_to_storage;

std::string normalize_archive_name(const std::filesystem::path& path) {
    std::filesystem::path normalized = path.lexically_normal();
    normalized.replace_extension();

    std::string name = normalized.generic_string();
    while (!name.empty() && name.front() == '/') {
        name.erase(name.begin());
    }

    return name;
}

std::expected<std::vector<uint8_t>, std::string> build_wrapper(
    std::string_view table_name,
    std::span<const uint8_t> payload,
    std::optional<bool> looped = std::nullopt
) {
    utf::UtfTable wrapper = utf::UtfTable::create(std::string(table_name));
    wrapper.add_column("data", utf::ColumnType::VLData);
    if (looped) wrapper.add_column("lpflg", utf::ColumnType::UInt8);

    const uint32_t row = wrapper.add_row();
    wrapper.set(row, "data", std::vector<uint8_t>(payload.begin(), payload.end())).value();
    if (looped) wrapper.set(row, "lpflg", static_cast<uint8_t>(*looped)).value();
    return wrapper.build();
}

std::expected<BuildStreamInfo, std::string> inspect_stream_payload(
    std::string name,
    std::string name_raw,
    std::vector<uint8_t> payload
) {
    if (payload.empty()) {
        return std::unexpected("CSB build failed: input stream payload is empty");
    }

    BuildStreamInfo info;
    info.name = std::move(name);
    info.name_raw = std::move(name_raw);
    info.payload = std::move(payload);

    if (std::ranges::starts_with(info.payload, std::array<uint8_t, 4>{'@', 'U', 'T', 'F'})) {
        auto aax_wrapper = aax::AaxContainer::load(info.payload);
        if (aax_wrapper) {
            info.format = 0;
            info.wrapper_table_name = "AAX";
            info.channels = aax_wrapper->channels();
            info.sample_rate = aax_wrapper->sample_rate();
            info.sample_count = aax_wrapper->sample_count();
            info.looped = aax_wrapper->has_loop_segments();
            info.wrapper = std::move(info.payload);
            info.payload.clear();
            return info;
        }
    }

    if (std::ranges::starts_with(info.payload, std::array<uint8_t, 4>{'H', 'C', 'A', 0})) {
        auto hca = hca::Hca::load(info.payload);
        if (!hca) {
            return std::unexpected("CSB build failed: unsupported HCA payload: could not parse HCA header");
        }
        const auto& hca_info = hca->header();

        info.format = 6;
        info.wrapper_table_name = "HCA";
        info.channels = hca_info.fmt.channel_count;
        info.sample_rate = hca_info.fmt.sample_rate;
        info.sample_count = hca_info.sample_count();
        info.looped = hca_info.loop.enabled();
        return info;
    }

    if (info.payload.size() >= 0x14 && io::read_be<uint16_t>(info.payload.data()) == 0x8000) {
        const uint8_t encoding_type = info.payload[0x04];
        if (encoding_type == 0x10 || encoding_type == 0x11) {
            if (info.payload[0x05] != 0 || info.payload[0x06] != 0 || info.payload[0x12] != 0x06) {
                return std::unexpected("CSB build failed: unsupported AHX payload header");
            }

            info.format = 2;
            info.wrapper_table_name = "AHX";
            info.channels = info.payload[0x07];
            info.sample_rate = io::read_be<uint32_t>(info.payload.data() + 0x08);
            info.sample_count = io::read_be<uint32_t>(info.payload.data() + 0x0C);
            info.looped = false;
            return info;
        }

        adx::AdxDecoder decoder;
        auto load_result = decoder.load(info.payload);
        if (!load_result) {
            return std::unexpected("CSB build failed: unsupported ADX/AAX payload: " + load_result.error());
        }

        const auto& header = decoder.header();
        info.format = 0;
        info.wrapper_table_name = "AAX";
        info.channels = header.channels;
        info.sample_rate = header.sample_rate;
        info.sample_count = header.sample_count;
        info.looped = decoder.has_loops();
        return info;
    }

    return std::unexpected("CSB build failed: unsupported stream payload type");
}

std::expected<std::vector<uint8_t>, std::string> build_wrapper_for_stream(const BuildStreamInfo& info) {
    if (!info.wrapper.empty()) {
        return info.wrapper;
    }

    switch (info.format) {
        case 0:
            return build_wrapper("AAX", info.payload, info.looped);
        case 6:
            return build_wrapper("HCA", info.payload, info.looped);
        case 2:
            return build_wrapper("AHX", info.payload);
        default:
            return std::unexpected("CSB build failed: unsupported stream format");
    }
}

std::expected<std::vector<BuildStreamInfo>, std::string> inspect_build_entries(
    std::span<const CsbBuildEntry> entries,
    const text::EncodingOptions& encoding
) {
    if (entries.empty()) {
        return std::unexpected("CSB build failed: no input entries were provided");
    }

    std::vector<BuildStreamInfo> streams;
    streams.reserve(entries.size());

    util::flat_unordered_set<std::string> seen_names;
    seen_names.reserve(entries.size());

    for (const auto& entry : entries) {
        if (entry.source_path.empty()) {
            return std::unexpected("CSB build entry has an empty source path");
        }

        const std::filesystem::path archive_path =
            entry.archive_path.empty() ? entry.source_path.filename() : entry.archive_path;
        const std::string archive_name = normalize_archive_name(archive_path);
        if (archive_name.empty()) {
            return std::unexpected("CSB build entry produced an empty archive path");
        }
        auto archive_name_raw = encode_cri_string_to_storage(
            archive_name,
            encoding,
            "CSB archive path encode failed"
        );
        if (!archive_name_raw) {
            return std::unexpected(archive_name_raw.error());
        }

        if (!seen_names.emplace(archive_name).second) {
            return std::unexpected("CSB build failed: duplicate archive path: " + archive_name);
        }

        auto payload = io::read_file_bytes(entry.source_path, "CSB build failed");
        if (!payload) {
            return std::unexpected(payload.error());
        }

        auto stream_info = inspect_stream_payload(archive_name, std::move(*archive_name_raw), std::move(*payload));
        if (!stream_info) {
            return std::unexpected("CSB build failed: could not inspect '" + archive_name + "': " + stream_info.error());
        }

        streams.push_back(std::move(*stream_info));
    }

    return streams;
}

std::expected<std::vector<uint8_t>, std::string> build_minimal_csb(const std::vector<BuildStreamInfo>& streams) {
    utf::UtfTable sound_element = utf::UtfTable::create("TBLSDL");
    sound_element.add_column("name", utf::ColumnType::String);
    sound_element.add_column("data", utf::ColumnType::VLData);
    sound_element.add_column("fmt", utf::ColumnType::UInt8);
    sound_element.add_column("nch", utf::ColumnType::UInt8);
    sound_element.add_column("stmflg", utf::ColumnType::UInt8);
    sound_element.add_column("sfreq", utf::ColumnType::UInt32);
    sound_element.add_column("nsmpl", utf::ColumnType::UInt32);

    for (const auto& stream : streams) {
        auto wrapper = build_wrapper_for_stream(stream);
        if (!wrapper) {
            return std::unexpected("CSB build failed: could not build wrapper for '" + stream.name + "': " + wrapper.error());
        }

        const uint32_t row = sound_element.add_row();
        sound_element.set(row, "name", stream.name_raw.empty() ? stream.name : stream.name_raw).value();
        sound_element.set(row, "data", std::move(*wrapper)).value();
        sound_element.set(row, "fmt", stream.format).value();
        sound_element.set(row, "nch", stream.channels).value();
        sound_element.set(row, "stmflg", static_cast<uint8_t>(0)).value();
        sound_element.set(row, "sfreq", stream.sample_rate).value();
        sound_element.set(row, "nsmpl", stream.sample_count).value();
    }

    std::vector<uint8_t> sound_element_bytes = sound_element.build();

    utf::UtfTable root = utf::UtfTable::create("TBLCSB");
    root.add_column("name", utf::ColumnType::String);
    root.add_column("ttype", utf::ColumnType::UInt8);
    root.add_column("utf", utf::ColumnType::VLData);

    const uint32_t row = root.add_row();
    root.set(row, "name", std::string("SOUND_ELEMENT")).value();
    root.set(row, "ttype", static_cast<uint8_t>(4)).value();
    root.set(row, "utf", std::move(sound_element_bytes)).value();
    return root.build();
}

std::expected<void, std::string> set_stream_row(
    utf::UtfTable& table,
    uint32_t row,
    const BuildStreamInfo& stream
) {
    auto wrapper = build_wrapper_for_stream(stream);
    if (!wrapper) {
        return std::unexpected("CSB edit failed: could not build stream wrapper: " + wrapper.error());
    }
    const auto set_required = [&table, row](std::string_view column, utf::Value value) -> std::expected<void, std::string> {
        if (table.find_column(column) < 0) {
            return std::unexpected("CSB edit failed: SOUND_ELEMENT is missing column '" + std::string(column) + "'");
        }
        return table.set(row, column, std::move(value));
    };

    std::array<std::pair<std::string_view, utf::Value>, 7> fields{{
        {"name", stream.name_raw.empty() ? stream.name : stream.name_raw},
        {"data", std::move(*wrapper)},
        {"fmt", stream.format},
        {"nch", stream.channels},
        {"stmflg", uint8_t{0}},
        {"sfreq", stream.sample_rate},
        {"nsmpl", stream.sample_count},
    }};
    for (auto& [column, value] : fields) {
        if (auto result = set_required(column, std::move(value)); !result) return result;
    }
    return {};
}

std::expected<void, std::string> write_csb(
    std::expected<std::vector<uint8_t>, std::string> bytes,
    const std::filesystem::path& output_path
) {
    if (!bytes) return std::unexpected(bytes.error());
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    return io::write_file_bytes(output_path, *bytes, "CSB build failed");
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> CsbContainer::build(
    std::span<const CsbBuildEntry> entries,
    const text::EncodingOptions& encoding
) {
    auto streams = inspect_build_entries(entries, encoding);
    if (!streams) {
        return std::unexpected(streams.error());
    }

    return build_minimal_csb(*streams);
}

std::expected<std::vector<uint8_t>, std::string> CsbContainer::build_from_directory(
    const std::filesystem::path& input_dir,
    const text::EncodingOptions& encoding
) {
    std::error_code ec;
    if (!std::filesystem::exists(input_dir, ec) || ec) {
        return std::unexpected("CSB input directory does not exist: " + input_dir.string());
    }
    if (!std::filesystem::is_directory(input_dir, ec) || ec) {
        return std::unexpected("CSB input path is not a directory: " + input_dir.string());
    }

    std::vector<CsbBuildEntry> entries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(input_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        auto relative = std::filesystem::relative(entry.path(), input_dir, ec);
        if (ec) {
            return std::unexpected("CSB build failed: could not compute relative path for " + entry.path().string());
        }

        entries.push_back({entry.path(), relative});
    }

    if (entries.empty()) {
        return std::unexpected("CSB input directory contains no files");
    }

    std::ranges::sort(entries, {}, [](const CsbBuildEntry& entry) {
        return normalize_archive_name(entry.archive_path);
    });

    return build(entries, encoding);
}

std::expected<void, std::string> CsbContainer::build_to_file(
    std::span<const CsbBuildEntry> entries,
    const std::filesystem::path& output_path,
    const text::EncodingOptions& encoding
) {
    return write_csb(build(entries, encoding), output_path);
}

std::expected<void, std::string> CsbContainer::replace_sound_element(utf::UtfTable sound_element) {
    const auto section = std::ranges::find_if(m_sections, [](const CsbSection& value) {
        return value.name == "SOUND_ELEMENT";
    });
    if (section == m_sections.end()) {
        return std::unexpected("CSB edit failed: SOUND_ELEMENT section is missing");
    }
    auto header = m_header.editable_copy();
    if (auto result = header.set(section->row_index, "utf", sound_element.build()); !result) {
        return std::unexpected("CSB edit failed: could not update SOUND_ELEMENT section: " + result.error());
    }
    auto replacement = load(header.build(), m_encoding);
    if (!replacement) {
        return std::unexpected("CSB edit failed: rebuilt container did not reload: " + replacement.error());
    }
    replacement->m_source_path = m_source_path;
    *this = std::move(*replacement);
    return {};
}

std::expected<void, std::string> CsbContainer::add_file(
    std::span<const uint8_t> bytes,
    const std::filesystem::path& archive_path
) {
    const auto name = normalize_archive_name(archive_path);
    if (name.empty()) {
        return std::unexpected("CSB add failed: archive path is empty");
    }
    if (std::ranges::any_of(m_elements, [&name](const CsbStreamInfo& element) { return element.name == name; })) {
        return std::unexpected("CSB add failed: duplicate archive path: " + name);
    }
    auto raw_name = encode_cri_string_to_storage(name, m_encoding, "CSB archive path encode failed");
    if (!raw_name) {
        return std::unexpected(raw_name.error());
    }
    auto stream = inspect_stream_payload(
        name,
        std::move(*raw_name),
        std::vector<uint8_t>(bytes.begin(), bytes.end()));
    if (!stream) {
        return std::unexpected(stream.error());
    }
    auto table = m_sound_element.editable_copy();
    const auto row = table.add_row();
    if (auto result = set_stream_row(table, row, *stream); !result) {
        return result;
    }
    return replace_sound_element(std::move(table));
}

std::expected<void, std::string> CsbContainer::replace_file(uint32_t index, std::span<const uint8_t> bytes) {
    if (index >= stream_count()) {
        return std::unexpected("CSB replace failed: stream index is out of range");
    }
    const auto& current = stream(index);
    auto stream_info = inspect_stream_payload(
        current.name,
        current.name_raw,
        std::vector<uint8_t>(bytes.begin(), bytes.end()));
    if (!stream_info) {
        return std::unexpected(stream_info.error());
    }
    auto table = m_sound_element.editable_copy();
    if (auto result = set_stream_row(table, current.row_index, *stream_info); !result) {
        return result;
    }
    if (auto result = table.set(
            current.row_index,
            "stmflg",
            static_cast<uint8_t>(current.streamed ? 1 : 0)); !result) {
        return std::unexpected("CSB replace failed: could not preserve stream flag: " + result.error());
    }
    return replace_sound_element(std::move(table));
}

std::expected<void, std::string> CsbContainer::remove_file(uint32_t index) {
    if (index >= stream_count()) {
        return std::unexpected("CSB remove failed: stream index is out of range");
    }
    if (stream_count() == 1) {
        return std::unexpected("CSB remove failed: a CSB must retain at least one embedded stream");
    }
    auto table = m_sound_element.editable_copy();
    if (!table.remove_row(stream(index).row_index)) {
        return std::unexpected("CSB remove failed: SOUND_ELEMENT row is out of range");
    }
    return replace_sound_element(std::move(table));
}

std::expected<void, std::string> CsbContainer::move_file(uint32_t from_index, uint32_t to_index) {
    if (from_index >= stream_count() || to_index >= stream_count()) {
        return std::unexpected("CSB move failed: stream index is out of range");
    }
    auto table = m_sound_element.editable_copy();
    if (!table.move_row(stream(from_index).row_index, stream(to_index).row_index)) {
        return std::unexpected("CSB move failed: SOUND_ELEMENT row is out of range");
    }
    return replace_sound_element(std::move(table));
}

std::expected<void, std::string> CsbContainer::rename_file(
    uint32_t index,
    const std::filesystem::path& archive_path
) {
    if (index >= stream_count()) {
        return std::unexpected("CSB rename failed: stream index is out of range");
    }
    const auto name = normalize_archive_name(archive_path);
    if (name.empty()) {
        return std::unexpected("CSB rename failed: archive path is empty");
    }
    auto raw_name = encode_cri_string_to_storage(name, m_encoding, "CSB archive path encode failed");
    if (!raw_name) {
        return std::unexpected(raw_name.error());
    }
    auto table = m_sound_element.editable_copy();
    if (auto result = table.set(stream(index).row_index, "name", std::move(*raw_name)); !result) {
        return std::unexpected("CSB rename failed: " + result.error());
    }
    return replace_sound_element(std::move(table));
}

std::expected<void, std::string> CsbContainer::set_streamed(uint32_t index, bool streamed) {
    if (index >= stream_count()) {
        return std::unexpected("CSB stream flag edit failed: stream index is out of range");
    }
    return set_element_streamed(stream(index).row_index, streamed);
}

std::expected<void, std::string> CsbContainer::set_element_streamed(uint32_t index, bool streamed) {
    if (index >= element_count()) {
        return std::unexpected("CSB stream flag edit failed: element index is out of range");
    }
    auto table = m_sound_element.editable_copy();
    if (auto result = table.set(
            element(index).row_index,
            "stmflg",
            static_cast<uint8_t>(streamed ? 1 : 0)); !result) {
        return std::unexpected("CSB stream flag edit failed: " + result.error());
    }
    return replace_sound_element(std::move(table));
}

std::expected<void, std::string> CsbContainer::build_to_file(
    const std::filesystem::path& input_dir,
    const std::filesystem::path& output_path,
    const text::EncodingOptions& encoding
) {
    return write_csb(build_from_directory(input_dir, encoding), output_path);
}

} // namespace cricodecs::csb
