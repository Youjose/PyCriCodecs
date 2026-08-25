/**
 * @file cvm_builder.cpp
 * @brief CVM/ROFS image builder.
 *
 * CVMH/ZONE layout and TOC scrambling follow `cvm_tool`.
 */

#include "cvm_builder.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "cvm_crypto.hpp"
#include "cvm_path.hpp"
#include "../utilities/flat_unordered_map.hpp"
#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"
#include "../utilities/scan.hpp"
#include "../utilities/string.hpp"

namespace cricodecs::cvm {

namespace {

using io::write_be;
using io::write_le;
using util::align_up;
using util::trim_ascii;
using util::uppercase_ascii;

constexpr uint32_t sector_size = 0x800u;
constexpr uint32_t cvmh_payload_size = 0x7F4u;
constexpr uint32_t cvmh_total_size = sector_size;
constexpr uint32_t zone_header_offset = cvmh_total_size;
constexpr uint32_t zone_payload_size = 0x7F4u;
constexpr uint32_t reserved_data_chunk_sector = 2u;
constexpr uint32_t iso_start_sector = 3u;
constexpr uint32_t pvd_sector = 16u;
constexpr uint32_t terminator_sector = 17u;
constexpr uint16_t volume_sequence_number = 1u;
constexpr uint16_t volume_set_size = 1u;
constexpr uint8_t directory_flag = 0x02u;
constexpr size_t archive_name_max = 31u;
constexpr std::string_view default_maker_id = "ROFSBLD Ver.1.43 2002-11-26";

struct IsoDateTime {
    uint8_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    int8_t gmt_offset = 0;
};

struct BuildFileData {
    std::filesystem::path archive_path;
    std::vector<uint8_t> owned_data;
    std::span<const uint8_t> data;
    uint32_t extent_sector = 0;
};

struct BuildDirectoryData {
    std::filesystem::path archive_path;
    std::filesystem::path parent_path;
    std::string name;
    std::vector<size_t> child_directories;
    std::vector<size_t> child_files;
    uint32_t extent_sector = 0;
    uint32_t path_table_index = 0;
    uint32_t data_length = 0;
    std::vector<uint8_t> encoded;
};

struct BuildState {
    const CvmBuildInput* input;
    IsoDateTime recording_date;
    std::vector<BuildFileData> files;
    std::vector<BuildDirectoryData> directories;
    util::flat_unordered_map<std::string, size_t> directory_index_by_key;
};

[[nodiscard]] std::expected<void, std::string> validate_iso_identifier(
    std::string_view value,
    size_t max_length,
    std::string_view field_name
) {
    if (value.size() > max_length) {
        return std::unexpected(
            "CVM " + std::string(field_name) + " exceeds the " + std::to_string(max_length) + "-byte ISO limit"
        );
    }
    return {};
}

[[nodiscard]] std::expected<IsoDateTime, std::string> parse_recording_date(std::string_view value) {
    if (trim_ascii(value).empty()) {
        return IsoDateTime{};
    }

    IsoDateTime parsed{};
    int day = 0;
    int month = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int ignored_subsecond = 0;
    int gmt_offset = 0;
    if (util::sscanf(
            std::string(value).c_str(),
            "%d/%d/%d %d:%d:%d:%d:%d",
            &day,
            &month,
            &year,
            &hour,
            &minute,
            &second,
            &ignored_subsecond,
            &gmt_offset
        ) == 8) {
        if (year < 1900 || year > 2155) {
            return std::unexpected("CVM recording_date year is out of ISO9660 range");
        }
        parsed.year = static_cast<uint8_t>(year - 1900);
        parsed.month = static_cast<uint8_t>(month);
        parsed.day = static_cast<uint8_t>(day);
        parsed.hour = static_cast<uint8_t>(hour);
        parsed.minute = static_cast<uint8_t>(minute);
        parsed.second = static_cast<uint8_t>(second);
        parsed.gmt_offset = static_cast<int8_t>(gmt_offset);
        return parsed;
    }

    if (util::sscanf(
            std::string(value).c_str(),
            "%d-%d-%d %d:%d:%d",
            &year,
            &month,
            &day,
            &hour,
            &minute,
            &second
        ) == 6) {
        if (year < 1900 || year > 2155) {
            return std::unexpected("CVM recording_date year is out of ISO9660 range");
        }
        parsed.year = static_cast<uint8_t>(year - 1900);
        parsed.month = static_cast<uint8_t>(month);
        parsed.day = static_cast<uint8_t>(day);
        parsed.hour = static_cast<uint8_t>(hour);
        parsed.minute = static_cast<uint8_t>(minute);
        parsed.second = static_cast<uint8_t>(second);
        parsed.gmt_offset = 0;
        return parsed;
    }

    return std::unexpected(
        "CVM recording_date must use 'DD/MM/YYYY HH:MM:SS:FF:TZ' or 'YYYY-MM-DD HH:MM:SS'"
    );
}

void write_iso_date_bytes(uint8_t* destination, const IsoDateTime& date) {
    destination[0] = date.year;
    destination[1] = date.month;
    destination[2] = date.day;
    destination[3] = date.hour;
    destination[4] = date.minute;
    destination[5] = date.second;
    destination[6] = static_cast<uint8_t>(date.gmt_offset);
}

void write_padded_ascii(std::span<uint8_t> field, std::string_view value) {
    std::fill(field.begin(), field.end(), static_cast<uint8_t>(' '));
    const size_t count = std::min(field.size(), value.size());
    std::copy_n(reinterpret_cast<const uint8_t*>(value.data()), count, field.data());
}

void write_both_endian_16(uint8_t* destination, uint16_t value) {
    write_le<uint16_t>(destination, value);
    write_be<uint16_t>(destination + sizeof(uint16_t), value);
}

void write_both_endian_32(uint8_t* destination, uint32_t value) {
    write_le<uint32_t>(destination, value);
    write_be<uint32_t>(destination + sizeof(uint32_t), value);
}

[[nodiscard]] size_t directory_record_length(size_t name_length) {
    size_t length = 33u + name_length;
    if ((name_length % 2u) == 0u) {
        ++length;
    }
    return length;
}

[[nodiscard]] std::vector<std::string> split_archive_components(const std::filesystem::path& path) {
    std::vector<std::string> components;
    for (const auto& part : path) {
        const std::string component = part.generic_string();
        if (!component.empty() && component != ".") {
            components.push_back(component);
        }
    }
    return components;
}

[[nodiscard]] std::expected<void, std::string> validate_archive_component(
    std::string_view component,
    std::string_view kind
) {
    if (component.empty() || component == "." || component == "..") {
        return std::unexpected("CVM " + std::string(kind) + " name must be a normal archive path component");
    }
    if (component.size() > archive_name_max) {
        return std::unexpected(
            "CVM " + std::string(kind) + " name exceeds the 31-character ROFS limit: " + std::string(component)
        );
    }
    if (component.find('/') != std::string_view::npos || component.find('\\') != std::string_view::npos) {
        return std::unexpected("CVM " + std::string(kind) + " name must not contain path separators");
    }
    return {};
}

[[nodiscard]] std::expected<size_t, std::string> ensure_directory(BuildState& state, const std::filesystem::path& path) {
    const std::string key = normalize_archive_lookup_key(path);
    const auto existing = state.directory_index_by_key.find(key);
    if (existing != state.directory_index_by_key.end()) {
        return existing->second;
    }

    BuildDirectoryData directory;
    directory.archive_path = normalize_archive_path(path);
    directory.parent_path = directory.archive_path.parent_path();
    directory.name = directory.archive_path.filename().generic_string();

    auto component_check = validate_archive_component(directory.name, "directory");
    if (!component_check) {
        return std::unexpected(component_check.error());
    }

    auto parent_index = ensure_directory(state, directory.parent_path);
    if (!parent_index) {
        return std::unexpected(parent_index.error());
    }

    const size_t index = state.directories.size();
    state.directories.push_back(std::move(directory));
    state.directory_index_by_key.emplace(key, index);
    state.directories[*parent_index].child_directories.push_back(index);
    return index;
}

[[nodiscard]] std::expected<BuildState, std::string> build_state_from_input(const CvmBuildInput& input) {
    if (input.files.empty()) {
        return std::unexpected("CVM build requires at least one file");
    }
    if (input.disc_name.empty()) {
        return std::unexpected("CVM disc_name must not be empty");
    }
    if (!std::filesystem::path(input.disc_name).has_extension() ||
        uppercase_ascii(std::filesystem::path(input.disc_name).extension().string()) != ".CVM") {
        return std::unexpected("CVM disc_name must end with .cvm");
    }
    if (!input.media.empty()) {
        const std::string media = uppercase_ascii(input.media);
        if (media != "CD" && media != "DVD") {
            return std::unexpected("CVM builder supports only official ROFS media values 'CD' or 'DVD'");
        }
    }

    auto recording_date = parse_recording_date(input.recording_date);
    if (!recording_date) {
        return std::unexpected(recording_date.error());
    }

    auto disc_name_check = validate_iso_identifier(input.disc_name, 32, "disc_name");
    if (!disc_name_check) {
        return std::unexpected(disc_name_check.error());
    }
    for (const auto& [value, limit, name] : {
             std::tuple{std::string_view(input.system_identifier), size_t{32}, std::string_view("system_identifier")},
             std::tuple{std::string_view(input.volume_identifier), size_t{32}, std::string_view("volume_identifier")},
             std::tuple{std::string_view(input.volume_set_identifier), size_t{128}, std::string_view("volume_set_identifier")},
             std::tuple{std::string_view(input.publisher_identifier), size_t{128}, std::string_view("publisher_identifier")},
             std::tuple{std::string_view(input.data_preparer_identifier), size_t{128}, std::string_view("data_preparer_identifier")},
             std::tuple{std::string_view(input.application_identifier), size_t{128}, std::string_view("application_identifier")},
         }) {
        auto check = validate_iso_identifier(value, limit, name);
        if (!check) {
            return std::unexpected(check.error());
        }
    }

    BuildState state{.input = &input};
    state.recording_date = *recording_date;
    state.directories.push_back({});
    state.directories[0].archive_path.clear();
    state.directories[0].parent_path.clear();
    state.directories[0].name.clear();
    state.directory_index_by_key.emplace("", 0u);

    util::flat_unordered_map<std::string, size_t> file_index_by_key;
    state.directory_index_by_key.reserve(input.files.size() + 1u);
    file_index_by_key.reserve(input.files.size());
    state.files.reserve(input.files.size());
    for (const auto& file : input.files) {
        const std::string normalized_archive_path = normalize_archive_path(file.archive_path);
        if (normalized_archive_path.empty()) {
            return std::unexpected("CVM file archive_path must not be empty");
        }
        const std::filesystem::path archive_path = normalized_archive_path;
        const auto components = split_archive_components(archive_path);
        for (size_t component_index = 0; component_index < components.size(); ++component_index) {
            auto component_check = validate_archive_component(
                components[component_index],
                component_index + 1u == components.size() ? "file" : "directory"
            );
            if (!component_check) {
                return std::unexpected(component_check.error());
            }
        }

        const std::string key = normalize_archive_lookup_key(archive_path);
        if (file_index_by_key.contains(key)) {
            return std::unexpected("CVM build failed: duplicate archive path: " + normalized_archive_path);
        }

        std::vector<uint8_t> owned_file_bytes;
        std::span<const uint8_t> file_bytes;
        if (file.data.has_value()) {
            file_bytes = std::span<const uint8_t>(file.data->data(), file.data->size());
        } else if (file.data_span.has_value()) {
            file_bytes = *file.data_span;
        } else {
            auto loaded_bytes = io::read_file_bytes(file.source_path, "CVM build source read failed");
            if (!loaded_bytes) {
                return std::unexpected(loaded_bytes.error());
            }
            owned_file_bytes = std::move(*loaded_bytes);
            file_bytes = std::span<const uint8_t>(owned_file_bytes.data(), owned_file_bytes.size());
        }

        auto parent_index = ensure_directory(state, archive_path.parent_path());
        if (!parent_index) {
            return std::unexpected(parent_index.error());
        }

        const size_t file_index = state.files.size();
        state.files.push_back({
            .archive_path = archive_path,
            .owned_data = std::move(owned_file_bytes),
            .data = std::move(file_bytes),
            .extent_sector = 0,
        });
        state.directories[*parent_index].child_files.push_back(file_index);
        file_index_by_key.emplace(key, file_index);
    }

    return state;
}

[[nodiscard]] uint32_t compute_toc_end_sector(const BuildState& state) {
    uint32_t toc_end_sector = terminator_sector + 1u;
    for (const auto& directory : state.directories) {
        toc_end_sector = std::max(toc_end_sector, directory.extent_sector + directory.data_length / sector_size);
    }
    return toc_end_sector;
}

void sort_directory_children(BuildState& state) {
    auto key_of_directory = [&](size_t index) {
        return uppercase_ascii(state.directories[index].name);
    };
    auto key_of_file = [&](size_t index) {
        return uppercase_ascii(state.files[index].archive_path.filename().generic_string());
    };

    for (auto& directory : state.directories) {
        std::sort(directory.child_directories.begin(), directory.child_directories.end(), [&](size_t lhs, size_t rhs) {
            return key_of_directory(lhs) < key_of_directory(rhs);
        });
        if (!state.input->preserve_file_order) {
            std::sort(directory.child_files.begin(), directory.child_files.end(), [&](size_t lhs, size_t rhs) {
                return key_of_file(lhs) < key_of_file(rhs);
            });
        }
    }
}

uint32_t compute_directory_size_recursive(BuildState& state, size_t directory_index) {
    auto& directory = state.directories[directory_index];
    size_t current_size = 0;
    auto append_record = [&](size_t record_name_length) {
        const size_t record_length = directory_record_length(record_name_length);
        const size_t sector_offset = current_size % sector_size;
        if (sector_offset + record_length > sector_size) {
            current_size += sector_size - sector_offset;
        }
        current_size += record_length;
    };

    append_record(1u);
    append_record(1u);
    for (const size_t child_directory_index : directory.child_directories) {
        compute_directory_size_recursive(state, child_directory_index);
        append_record(state.directories[child_directory_index].name.size());
    }
    for (const size_t child_file_index : directory.child_files) {
        const std::string file_name = state.files[child_file_index].archive_path.filename().generic_string() + ";1";
        append_record(file_name.size());
    }

    directory.data_length = static_cast<uint32_t>(align_up(current_size, static_cast<size_t>(sector_size)));
    if (directory.data_length == 0) {
        directory.data_length = sector_size;
    }
    return directory.data_length;
}

void assign_path_table_indices_recursive(BuildState& state, size_t directory_index, uint32_t& next_index) {
    state.directories[directory_index].path_table_index = next_index++;
    for (const size_t child_directory_index : state.directories[directory_index].child_directories) {
        assign_path_table_indices_recursive(state, child_directory_index, next_index);
    }
}

void assign_extents(BuildState& state, uint32_t first_directory_sector) {
    uint32_t current_sector = first_directory_sector;
    for (auto& directory : state.directories) {
        directory.extent_sector = current_sector;
        current_sector += directory.data_length / sector_size;
    }
    for (auto& file : state.files) {
        file.extent_sector = current_sector;
        current_sector += static_cast<uint32_t>(align_up(file.data.size(), static_cast<size_t>(sector_size)) / sector_size);
    }
}

void append_directory_record(
    std::span<uint8_t> buffer,
    size_t& current_offset,
    std::span<const uint8_t> name_bytes,
    uint32_t extent_sector,
    uint32_t data_length,
    bool is_directory,
    const IsoDateTime& recording_date
) {
    const size_t record_length = directory_record_length(name_bytes.size());
    const size_t sector_offset = current_offset % sector_size;
    if (sector_offset + record_length > sector_size) {
        std::fill(buffer.begin() + static_cast<std::ptrdiff_t>(current_offset),
                  buffer.begin() + static_cast<std::ptrdiff_t>(current_offset + (sector_size - sector_offset)),
                  uint8_t{0});
        current_offset += sector_size - sector_offset;
    }

    uint8_t* record = buffer.data() + current_offset;
    std::fill(record, record + record_length, uint8_t{0});
    record[0] = static_cast<uint8_t>(record_length);
    record[1] = 0;
    write_le<uint32_t>(record + 2, extent_sector);
    write_be<uint32_t>(record + 6, extent_sector);
    write_le<uint32_t>(record + 10, data_length);
    write_be<uint32_t>(record + 14, data_length);
    write_iso_date_bytes(record + 18, recording_date);
    record[25] = is_directory ? directory_flag : 0u;
    record[26] = 0;
    record[27] = 0;
    write_both_endian_16(record + 28, volume_sequence_number);
    record[32] = static_cast<uint8_t>(name_bytes.size());
    if (!name_bytes.empty()) {
        std::copy(name_bytes.begin(), name_bytes.end(), record + 33);
    }
    current_offset += record_length;
}

std::vector<uint8_t> encode_directory(const BuildState& state, size_t directory_index) {
    const auto& directory = state.directories[directory_index];
    std::vector<uint8_t> encoded(directory.data_length, 0);
    size_t current_offset = 0;

    constexpr uint8_t root_identifier = 0x00;
    constexpr uint8_t parent_identifier = 0x01;
    append_directory_record(
        encoded,
        current_offset,
        std::span<const uint8_t>(&root_identifier, 1),
        directory.extent_sector,
        directory.data_length,
        true,
        state.recording_date
    );

    const auto& parent = state.directories[
        is_root_archive_path(directory.archive_path) ? directory_index : state.directory_index_by_key.at(normalize_archive_lookup_key(directory.parent_path))
    ];
    append_directory_record(
        encoded,
        current_offset,
        std::span<const uint8_t>(&parent_identifier, 1),
        parent.extent_sector,
        parent.data_length,
        true,
        state.recording_date
    );

    for (const size_t child_directory_index : directory.child_directories) {
        const auto& child_directory = state.directories[child_directory_index];
        const auto name_bytes = std::as_bytes(std::span<const char>(child_directory.name.data(), child_directory.name.size()));
        append_directory_record(
            encoded,
            current_offset,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(name_bytes.data()), name_bytes.size()),
            child_directory.extent_sector,
            child_directory.data_length,
            true,
            state.recording_date
        );
    }

    for (const size_t child_file_index : directory.child_files) {
        const auto& child_file = state.files[child_file_index];
        const std::string iso_name = child_file.archive_path.filename().generic_string() + ";1";
        append_directory_record(
            encoded,
            current_offset,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(iso_name.data()), iso_name.size()),
            child_file.extent_sector,
            static_cast<uint32_t>(child_file.data.size()),
            false,
            state.recording_date
        );
    }

    return encoded;
}

std::vector<uint8_t> encode_path_table(const BuildState& state, bool big_endian) {
    std::vector<uint8_t> table;
    table.reserve(state.directories.size() * 16u);

    for (size_t directory_index = 0; directory_index < state.directories.size(); ++directory_index) {
        const auto& directory = state.directories[directory_index];
        const bool is_root = is_root_archive_path(directory.archive_path);
        const std::string identifier = is_root ? std::string(1, '\0') : directory.name;
        const uint8_t identifier_length = static_cast<uint8_t>(identifier.size());
        const size_t entry_offset = table.size();
        table.resize(entry_offset + 8u + identifier_length + (identifier_length % 2u), 0);
        uint8_t* entry = table.data() + entry_offset;
        entry[0] = identifier_length;
        entry[1] = 0;
        if (big_endian) {
            write_be<uint32_t>(entry + 2, directory.extent_sector);
            const auto parent_index = is_root
                ? volume_sequence_number
                : static_cast<uint16_t>(state.directories[state.directory_index_by_key.at(normalize_archive_lookup_key(directory.parent_path))].path_table_index);
            write_be<uint16_t>(entry + 6, parent_index);
        } else {
            write_le<uint32_t>(entry + 2, directory.extent_sector);
            const auto parent_index = is_root
                ? volume_sequence_number
                : static_cast<uint16_t>(state.directories[state.directory_index_by_key.at(normalize_archive_lookup_key(directory.parent_path))].path_table_index);
            write_le<uint16_t>(entry + 6, parent_index);
        }
        if (!identifier.empty()) {
            std::copy(identifier.begin(), identifier.end(), reinterpret_cast<char*>(entry + 8));
        }
    }

    return table;
}

std::vector<uint8_t> build_embedded_iso(BuildState& state) {
    sort_directory_children(state);
    compute_directory_size_recursive(state, 0);
    uint32_t next_path_table_index = 1u;
    assign_path_table_indices_recursive(state, 0, next_path_table_index);

    std::vector<uint8_t> placeholder_path_table_le;
    std::vector<uint8_t> placeholder_path_table_be;
    uint32_t path_table_sector_le = terminator_sector + 1u;
    uint32_t path_table_sectors_le = 1u;
    uint32_t path_table_sector_be = path_table_sector_le + path_table_sectors_le;
    uint32_t path_table_sectors_be = 1u;
    uint32_t first_directory_sector = path_table_sector_be + path_table_sectors_be;

    for (;;) {
        assign_extents(state, first_directory_sector);
        auto path_table_le = encode_path_table(state, false);
        auto path_table_be = encode_path_table(state, true);
        const uint32_t needed_le = static_cast<uint32_t>(align_up(path_table_le.size(), static_cast<size_t>(sector_size)) / sector_size);
        const uint32_t needed_be = static_cast<uint32_t>(align_up(path_table_be.size(), static_cast<size_t>(sector_size)) / sector_size);
        if (needed_le == path_table_sectors_le && needed_be == path_table_sectors_be) {
            placeholder_path_table_le = std::move(path_table_le);
            placeholder_path_table_be = std::move(path_table_be);
            break;
        }
        path_table_sectors_le = needed_le;
        path_table_sectors_be = needed_be;
        path_table_sector_be = path_table_sector_le + path_table_sectors_le;
        first_directory_sector = path_table_sector_be + path_table_sectors_be;
    }

    for (size_t directory_index = 0; directory_index < state.directories.size(); ++directory_index) {
        state.directories[directory_index].encoded = encode_directory(state, directory_index);
    }

    uint32_t total_sectors = first_directory_sector;
    for (const auto& directory : state.directories) {
        total_sectors = std::max(total_sectors, directory.extent_sector + directory.data_length / sector_size);
    }
    for (const auto& file : state.files) {
        total_sectors = std::max(
            total_sectors,
            file.extent_sector + static_cast<uint32_t>(align_up(file.data.size(), static_cast<size_t>(sector_size)) / sector_size)
        );
    }

    std::vector<uint8_t> iso(static_cast<size_t>(total_sectors) * sector_size, 0);

    auto copy_sector_aligned = [&](uint32_t sector, std::span<const uint8_t> payload) {
        const size_t offset = static_cast<size_t>(sector) * sector_size;
        std::copy(payload.begin(), payload.end(), iso.begin() + static_cast<std::ptrdiff_t>(offset));
    };

    std::vector<uint8_t> pvd(sector_size, 0);
    pvd[0] = 0x01u;
    std::copy_n("CD001", 5, reinterpret_cast<char*>(pvd.data() + 1));
    pvd[6] = 0x01u;
    write_padded_ascii(std::span<uint8_t>(pvd.data() + 8, 32), state.input->system_identifier);
    write_padded_ascii(std::span<uint8_t>(pvd.data() + 40, 32), state.input->volume_identifier);
    write_both_endian_32(pvd.data() + 80, total_sectors);
    write_both_endian_16(pvd.data() + 120, volume_set_size);
    write_both_endian_16(pvd.data() + 124, volume_sequence_number);
    write_both_endian_16(pvd.data() + 128, static_cast<uint16_t>(sector_size));
    write_both_endian_32(pvd.data() + 132, static_cast<uint32_t>(placeholder_path_table_le.size()));
    write_le<uint32_t>(pvd.data() + 140, path_table_sector_le);
    write_le<uint32_t>(pvd.data() + 144, 0u);
    write_be<uint32_t>(pvd.data() + 148, path_table_sector_be);
    write_be<uint32_t>(pvd.data() + 152, 0u);
    size_t root_record_offset = 156u;
    constexpr uint8_t root_identifier = 0x00;
    append_directory_record(
        std::span<uint8_t>(pvd),
        root_record_offset,
        std::span<const uint8_t>(&root_identifier, 1),
        state.directories[0].extent_sector,
        state.directories[0].data_length,
        true,
        state.recording_date
    );
    write_padded_ascii(
        std::span<uint8_t>(pvd.data() + 190, 128),
        state.input->volume_set_identifier.empty() ? state.input->volume_identifier : state.input->volume_set_identifier
    );
    write_padded_ascii(std::span<uint8_t>(pvd.data() + 318, 128), state.input->publisher_identifier);
    write_padded_ascii(std::span<uint8_t>(pvd.data() + 446, 128), state.input->data_preparer_identifier);
    write_padded_ascii(std::span<uint8_t>(pvd.data() + 574, 128), state.input->application_identifier);
    pvd[881] = 0x01u;
    copy_sector_aligned(pvd_sector, pvd);

    std::vector<uint8_t> terminator(sector_size, 0);
    terminator[0] = 0xFFu;
    std::copy_n("CD001", 5, reinterpret_cast<char*>(terminator.data() + 1));
    terminator[6] = 0x01u;
    copy_sector_aligned(terminator_sector, terminator);

    copy_sector_aligned(path_table_sector_le, placeholder_path_table_le);
    copy_sector_aligned(path_table_sector_be, placeholder_path_table_be);

    for (const auto& directory : state.directories) {
        copy_sector_aligned(directory.extent_sector, directory.encoded);
    }
    for (const auto& file : state.files) {
        copy_sector_aligned(file.extent_sector, file.data);
    }

    return iso;
}

std::vector<uint8_t> build_cvm_image(BuildState& state) {
    std::vector<uint8_t> iso = build_embedded_iso(state);
    const uint64_t iso_size = iso.size();
    const uint64_t total_file_size = static_cast<uint64_t>(iso_start_sector) * sector_size + iso_size;
    const uint64_t zone_chunk_length = zone_payload_size + sector_size + iso_size;

    std::vector<uint8_t> image(static_cast<size_t>(total_file_size), 0);

    std::copy_n("CVMH", 4, reinterpret_cast<char*>(image.data()));
    write_be<uint64_t>(image.data() + 0x04, cvmh_payload_size);
    write_be<uint64_t>(image.data() + 0x1C, total_file_size);
    write_iso_date_bytes(image.data() + 0x24, state.recording_date);
    image[0x2C] = 0x01u;
    image[0x2D] = 0x01u;
    write_padded_ascii(std::span<uint8_t>(image.data() + 0x34, 4), "ROFS");
    write_padded_ascii(std::span<uint8_t>(image.data() + 0x38, 64), default_maker_id);
    image[0x78] = 0x01u;
    image[0x79] = 0x1Fu;
    image[0x7C] = 0x03u;
    write_be<uint32_t>(image.data() + 0x80, 1u);
    write_be<uint32_t>(image.data() + 0x84, 0u);
    write_be<uint32_t>(image.data() + 0x88, iso_start_sector);
    write_be<uint32_t>(image.data() + 0x100, 1u);

    std::copy_n("ZONE", 4, reinterpret_cast<char*>(image.data() + zone_header_offset));
    write_be<uint64_t>(image.data() + zone_header_offset + 0x04, zone_chunk_length);
    write_be<uint32_t>(image.data() + zone_header_offset + 0x0C, iso_start_sector);
    write_be<uint32_t>(image.data() + zone_header_offset + 0x10, 0x410u);
    write_be<uint32_t>(image.data() + zone_header_offset + 0x1C, sector_size);
    write_be<uint32_t>(image.data() + zone_header_offset + 0x20, reserved_data_chunk_sector);
    write_be<uint64_t>(image.data() + zone_header_offset + 0x24, sector_size);
    write_be<uint32_t>(image.data() + zone_header_offset + 0x2C, iso_start_sector);
    write_be<uint64_t>(image.data() + zone_header_offset + 0x30, iso_size);

    std::copy(iso.begin(), iso.end(), image.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(iso_start_sector) * sector_size));
    return image;
}

std::vector<uint8_t> scramble_cvm_toc(std::vector<uint8_t> image, const BuildState& state, std::string_view key) {
    if (key.empty()) {
        return image;
    }

    const auto scramble_key = crypto::calc_key_from_string(key);
    const uint32_t toc_end_sector = compute_toc_end_sector(state);
    const size_t toc_offset = static_cast<size_t>(iso_start_sector + pvd_sector) * sector_size;
    const uint32_t toc_sector_count = toc_end_sector - pvd_sector;
    const size_t toc_size = static_cast<size_t>(toc_sector_count) * sector_size;
    crypto::transform_sectors(
        std::span<uint8_t>(image.data() + static_cast<std::ptrdiff_t>(toc_offset), toc_size),
        pvd_sector,
        toc_sector_count,
        sector_size,
        scramble_key
    );
    write_be<uint32_t>(image.data() + 0x30, 0x10u);
    return image;
}

} // namespace

CvmBuildInput CvmBuildInput::from_script(const CvmBuildScript& script) {
    CvmBuildInput input;
    input.disc_name = script.disc_name();
    input.recording_date = script.recording_date();
    input.media = script.media();
    input.system_identifier = script.system_identifier();
    input.volume_identifier = script.volume_identifier();
    input.volume_set_identifier = script.volume_set_identifier();
    input.publisher_identifier = script.publisher_identifier();
    input.data_preparer_identifier = script.data_preparer_identifier();
    input.application_identifier = script.application_identifier();
    input.files.reserve(script.files().size());
    for (const auto& file : script.files()) {
        input.files.push_back({
            .archive_path = file.archive_path,
            .source_path = file.source_path,
            .data = std::nullopt,
            .data_span = std::nullopt,
        });
    }
    return input;
}

std::expected<CvmBuildInput, std::string> CvmBuildInput::from_directory(
    const std::filesystem::path& input_dir,
    const CvmBuildDirectoryOptions& options
) {
    std::error_code filesystem_error;
    if (!std::filesystem::exists(input_dir, filesystem_error) ||
        !std::filesystem::is_directory(input_dir, filesystem_error)) {
        return std::unexpected("CVM input directory was not found: " + input_dir.string());
    }

    CvmBuildInput input;
    const std::string dir_name = input_dir.filename().generic_string().empty()
        ? std::string("volume")
        : input_dir.filename().generic_string();
    input.disc_name = options.disc_name.empty() ? dir_name + ".cvm" : options.disc_name;
    input.recording_date = options.recording_date;
    input.media = options.media;
    input.system_identifier = options.system_identifier;
    input.volume_identifier = options.volume_identifier;
    input.volume_set_identifier = options.volume_set_identifier;
    input.publisher_identifier = options.publisher_identifier;
    input.data_preparer_identifier = options.data_preparer_identifier;
    input.application_identifier = options.application_identifier;

    for (std::filesystem::recursive_directory_iterator it(input_dir, filesystem_error), end; it != end; it.increment(filesystem_error)) {
        if (filesystem_error) {
            return std::unexpected("CVM input directory walk failed: " + filesystem_error.message());
        }
        if (!it->is_regular_file()) {
            continue;
        }

        const std::filesystem::path relative_path = std::filesystem::relative(it->path(), input_dir, filesystem_error);
        if (filesystem_error) {
            return std::unexpected("CVM directory relative-path resolution failed: " + filesystem_error.message());
        }

        input.files.push_back({
            .archive_path = relative_path.generic_string(),
            .source_path = it->path(),
            .data = std::nullopt,
            .data_span = std::nullopt,
        });
    }

    struct KeyedFile {
        std::string key;
        CvmBuildFile file;
    };
    std::vector<KeyedFile> keyed_files;
    keyed_files.reserve(input.files.size());
    for (auto& file : input.files) {
        keyed_files.push_back({uppercase_ascii(file.archive_path.generic_string()), std::move(file)});
    }
    std::ranges::sort(keyed_files, {}, &KeyedFile::key);
    input.files.clear();
    input.files.reserve(keyed_files.size());
    for (auto& keyed_file : keyed_files) {
        input.files.push_back(std::move(keyed_file.file));
    }
    return input;
}

std::expected<std::vector<uint8_t>, std::string> CvmBuilder::build(
    const CvmBuildInput& input,
    std::string_view key
) const {
    return build_state_from_input(input).transform([&](auto state) {
        return scramble_cvm_toc(build_cvm_image(state), state, key);
    });
}

std::expected<void, std::string> CvmBuilder::build_to_file(
    const std::filesystem::path& output_path,
    const CvmBuildInput& input,
    std::string_view key
) const {
    auto image = build(input, key);
    if (!image) {
        return std::unexpected(image.error());
    }
    std::error_code filesystem_error;
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("CVM build failed: could not create output directory: " + filesystem_error.message());
        }
    }

    return io::write_file_bytes(output_path, *image, "CVM build failed");
}

std::expected<std::vector<uint8_t>, std::string> CvmBuilder::build(
    const CvmBuildScript& script,
    std::string_view key
) const {
    return build(CvmBuildInput::from_script(script), key);
}

std::expected<void, std::string> CvmBuilder::build_to_file(
    const std::filesystem::path& output_path,
    const CvmBuildScript& script,
    std::string_view key
) const {
    return build_to_file(output_path, CvmBuildInput::from_script(script), key);
}

} // namespace cricodecs::cvm
