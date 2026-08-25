/**
 * @file acx_builder.cpp
 * @brief ACX archive builder.
 *
 * ACX build behavior is based on official `adxcat` evidence and reviewed samples.
 * The C++23 builder surface and parity checks are CriCodecs work by Youjose.
 */

#include "acx_builder.hpp"

#include <fstream>
#include <limits>
#include <string_view>

#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"

namespace cricodecs::acx {
namespace {

using io::write_be;
using util::align_up_checked;

struct AcxBuildLayout {
    std::vector<uint32_t> offsets;
    uint64_t archive_size = 0;
};

[[nodiscard]] std::string trim_ascii_whitespace(std::string_view value) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos) return {};
    return std::string(value.substr(first, value.find_last_not_of(whitespace) - first + 1));
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> resolve_entry_bytes(const AcxBuildEntry& entry) {
    if (entry.data.has_value()) {
        return *entry.data;
    }
    if (entry.source_path.empty()) {
        return std::unexpected("ACX build failed: each entry needs either source_path or data");
    }
    auto bytes = io::read_file_bytes(entry.source_path, "ACX build failed");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return bytes;
}

[[nodiscard]] std::expected<uint32_t, std::string> entry_size(const AcxBuildEntry& entry) {
    if (entry.data.has_value()) {
        if (entry.data->size() > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected("ACX build failed: entry size exceeds supported range");
        }
        return static_cast<uint32_t>(entry.data->size());
    }
    if (entry.source_path.empty()) {
        return std::unexpected("ACX build failed: each entry needs either source_path or data");
    }

    std::error_code filesystem_error;
    const auto size = std::filesystem::file_size(entry.source_path, filesystem_error);
    if (filesystem_error) {
        return std::unexpected("ACX build failed: could not read input size: " + entry.source_path.string());
    }
    if (size > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("ACX build failed: entry size exceeds supported range");
    }
    return static_cast<uint32_t>(size);
}

[[nodiscard]] std::expected<AcxBuildLayout, std::string> make_layout(
    std::span<const uint32_t> sizes,
    uint32_t alignment)
{
    const uint64_t table_size = 0x08ull + static_cast<uint64_t>(sizes.size()) * 0x08ull;
    const auto first_offset = align_up_checked(table_size, alignment, "ACX build failed");
    if (!first_offset) {
        return std::unexpected(first_offset.error());
    }
    if (*first_offset > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("ACX build failed: first payload offset exceeds supported range");
    }

    AcxBuildLayout layout;
    layout.archive_size = *first_offset;
    layout.offsets.reserve(sizes.size());

    for (const uint32_t size : sizes) {
        if (layout.archive_size > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected("ACX build failed: entry offset exceeds supported range");
        }

        layout.offsets.push_back(static_cast<uint32_t>(layout.archive_size));

        const uint64_t raw_end = layout.archive_size + size;
        const auto aligned_end = align_up_checked(raw_end, alignment, "ACX build failed");
        if (!aligned_end) {
            return std::unexpected(aligned_end.error());
        }
        layout.archive_size = *aligned_end;
    }

    return layout;
}

std::expected<void, std::string> validate_input(const AcxBuildInput& input) {
    if (input.entries.empty()) {
        return std::unexpected("ACX build failed: no entries were provided");
    }
    if (input.alignment == 0) {
        return std::unexpected("ACX build failed: alignment must be non-zero");
    }
    if (input.entries.size() > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("ACX build failed: entry count exceeds supported range");
    }
    return {};
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> AcxBuilder::build(const AcxBuildInput& input) const {
    if (auto valid = validate_input(input); !valid) return std::unexpected(valid.error());

    std::vector<std::vector<uint8_t>> payloads;
    payloads.reserve(input.entries.size());
    for (const auto& entry : input.entries) {
        auto payload = resolve_entry_bytes(entry);
        if (!payload) {
            return std::unexpected(payload.error());
        }
        payloads.push_back(std::move(*payload));
    }

    std::vector<uint32_t> sizes;
    sizes.reserve(payloads.size());
    for (const auto& payload : payloads) {
        if (payload.size() > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected("ACX build failed: entry size exceeds supported range");
        }
        sizes.push_back(static_cast<uint32_t>(payload.size()));
    }

    auto layout = make_layout(sizes, input.alignment);
    if (!layout) {
        return std::unexpected(layout.error());
    }
    if (layout->archive_size > std::numeric_limits<size_t>::max()) {
        return std::unexpected("ACX build failed: archive size exceeds supported range");
    }

    std::vector<uint8_t> built(static_cast<size_t>(layout->archive_size), 0);
    write_be<uint32_t>(built.data(), 0);
    write_be<uint32_t>(built.data() + 0x04, static_cast<uint32_t>(payloads.size()));

    for (size_t index = 0; index < payloads.size(); ++index) {
        const size_t table_offset = 0x08u + index * 0x08u;
        write_be<uint32_t>(built.data() + table_offset + 0x00, layout->offsets[index]);
        write_be<uint32_t>(built.data() + table_offset + 0x04, sizes[index]);
        std::ranges::copy(payloads[index], built.begin() + static_cast<size_t>(layout->offsets[index]));
    }

    return built;
}

std::expected<void, std::string> AcxBuilder::build_to_file(
    const std::filesystem::path& output_path,
    const AcxBuildInput& input
) const {
    if (auto valid = validate_input(input); !valid) return std::unexpected(valid.error());

    std::vector<uint32_t> sizes;
    sizes.reserve(input.entries.size());
    for (const auto& entry : input.entries) {
        auto size = entry_size(entry);
        if (!size) {
            return std::unexpected(size.error());
        }
        sizes.push_back(*size);
    }

    auto layout = make_layout(sizes, input.alignment);
    if (!layout) {
        return std::unexpected(layout.error());
    }

    io::writer output;
    auto open_result = output.open(output_path);
    if (!open_result) {
        return std::unexpected("ACX build failed: could not open output: " + output_path.string());
    }

    output.write_be<uint32_t>(0);
    output.write_be<uint32_t>(static_cast<uint32_t>(input.entries.size()));
    for (size_t index = 0; index < input.entries.size(); ++index) {
        output.write_be<uint32_t>(layout->offsets[index]);
        output.write_be<uint32_t>(sizes[index]);
    }

    const size_t table_size = 0x08u + input.entries.size() * 0x08u;
    const uint64_t first_offset = layout->offsets.front();
    if (first_offset > table_size) {
        output.write_zeros(static_cast<size_t>(first_offset - table_size));
    }

    uint64_t cursor = first_offset;
    for (size_t index = 0; index < input.entries.size(); ++index) {
        const auto& entry = input.entries[index];
        if (entry.data.has_value()) {
            output.write_bytes(*entry.data);
        } else {
            auto bytes = io::read_file_bytes(entry.source_path, "ACX build failed");
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            output.write_bytes(*bytes);
        }

        cursor += sizes[index];
        const uint64_t next_offset = (index + 1 < input.entries.size())
            ? layout->offsets[index + 1]
            : layout->archive_size;
        if (next_offset > cursor) {
            output.write_zeros(static_cast<size_t>(next_offset - cursor));
            cursor = next_offset;
        }
    }

    auto close_result = output.close();
    if (!close_result) {
        return std::unexpected("ACX build failed: could not write output: " + output_path.string());
    }
    return {};
}

std::expected<AcxBuildInput, std::string> AcxBuilder::parse_file_list(
    const std::filesystem::path& file_list_path,
    uint32_t alignment
) {
    std::ifstream input(file_list_path);
    if (!input) {
        return std::unexpected("ACX file list load failed: " + file_list_path.string());
    }

    AcxBuildInput build_input;
    build_input.alignment = alignment;
    const auto base_dir = file_list_path.parent_path();

    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = trim_ascii_whitespace(line);
        if (trimmed.empty()) {
            continue;
        }

        AcxBuildEntry entry;
        entry.source_path = std::filesystem::path(trimmed);
        if (!entry.source_path.is_absolute()) {
            entry.source_path = base_dir / entry.source_path;
        }
        build_input.entries.push_back(std::move(entry));
    }

    if (build_input.entries.empty()) {
        return std::unexpected("ACX file list is empty");
    }
    return build_input;
}

std::expected<std::vector<uint8_t>, std::string> AcxBuilder::build_from_file_list(
    const std::filesystem::path& file_list_path,
    uint32_t alignment
) const {
    return parse_file_list(file_list_path, alignment).and_then(
        [this](const AcxBuildInput& input) { return build(input); });
}

std::expected<void, std::string> AcxBuilder::build_file_list_to_file(
    const std::filesystem::path& file_list_path,
    const std::filesystem::path& output_path,
    uint32_t alignment
) const {
    return parse_file_list(file_list_path, alignment).and_then(
        [&](const AcxBuildInput& input) { return build_to_file(output_path, input); });
}

} // namespace cricodecs::acx
