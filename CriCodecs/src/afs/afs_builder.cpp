/**
 * @file afs_builder.cpp
 * @brief Classic AFS archive builder and file-ID header export.
 *
 * Behavior is cross-checked against CRI AfsLink/afslnk tools, manuals.
 * The C++23 builder and header-mode validation are CriCodecs work by Youjose.
 */

#include "afs_container.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>

#include "afs_format.hpp"
#include "../utilities/io_endian.hpp"
#include "../utilities/io_reader.hpp"
#include "../utilities/io_writer.hpp"
#include "../utilities/numeric.hpp"
#include "../utilities/string.hpp"

namespace cricodecs::afs {

namespace {

using util::align_up;
using util::starts_with_case_insensitive;
using util::trim_ascii;

[[nodiscard]] std::string strip_optional_quotes(std::string_view text) {
    std::string trimmed = trim_ascii(text);
    if (trimmed.size() >= 2 &&
        ((trimmed.front() == '"' && trimmed.back() == '"') ||
         (trimmed.front() == '\'' && trimmed.back() == '\''))) {
        trimmed = trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

[[nodiscard]] std::expected<uint32_t, std::string> parse_u32_text(std::string_view text, std::string_view context) {
    uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::unexpected(std::string(context) + ": invalid unsigned integer");
    }
    return value;
}

[[nodiscard]] std::string normalize_macro_source_name(std::string_view raw_name) {
    std::string normalized_text(raw_name);
    std::ranges::replace(normalized_text, '\\', '/');

    std::filesystem::path normalized(normalized_text);
    normalized = normalized.lexically_normal();
    std::string text = normalized.generic_string();
    if (text.empty()) {
        text = std::move(normalized_text);
    }
    return text;
}

[[nodiscard]] std::filesystem::path normalize_generic_tool_path(std::string_view raw_path) {
    std::string normalized_text(raw_path);
    std::ranges::replace(normalized_text, '\\', '/');

    std::filesystem::path normalized(normalized_text);
    return normalized.lexically_normal();
}

[[nodiscard]] std::string join_tool_display_path(std::string_view base, std::string_view leaf) {
    if (base.empty()) return std::string(leaf);
    if (leaf.empty()) return std::string(base);

    std::string joined(base);
    const bool base_separator = joined.back() == '/' || joined.back() == '\\';
    const bool leaf_separator = leaf.front() == '/' || leaf.front() == '\\';
    if (!base_separator && !leaf_separator)
        joined.push_back(base.contains('\\') ? '\\' : '/');
    joined += base_separator && leaf_separator ? leaf.substr(1) : leaf;
    return joined;
}

[[nodiscard]] std::string normalize_windows_display_path(std::string_view path) {
    if (path.empty()) {
        return {};
    }

    const char separator = path.find('\\') != std::string_view::npos ? '\\' : '/';
    std::string normalized;
    normalized.reserve(path.size());
    for (char ch : path) {
        if (ch == '/' || ch == '\\') {
            if (!normalized.empty() && normalized.back() == separator) {
                continue;
            }
            normalized.push_back(separator);
            continue;
        }
        normalized.push_back(ch);
    }

    return normalized;
}

[[nodiscard]] std::filesystem::path archive_filename(std::string_view archive_name) {
    std::filesystem::path normalized = normalize_generic_tool_path(archive_name);
    auto filename = normalized.filename();
    return filename.empty() ? normalized : filename;
}

[[nodiscard]] std::string normalize_archive_header_banner_name(std::string_view archive_name) {
    auto text = archive_filename(archive_name).generic_string();
    return text.empty() ? std::string(archive_name) : text;
}

[[nodiscard]] std::string normalize_archive_header_macro_name(std::string_view archive_name) {
    const auto filename = archive_filename(archive_name);
    std::string text = filename.stem().generic_string();
    if (text.empty()) text = filename.generic_string();
    return text.empty() ? std::string(archive_name) : text;
}

[[nodiscard]] size_t filename_offset(std::string_view name) {
    const auto separator = name.find_last_of("/\\");
    if (separator == std::string_view::npos) return 0;
    return separator + 1;
}

[[nodiscard]] bool has_windows_drive_prefix(std::string_view name) {
    return name.size() >= 3 &&
           std::isalpha(static_cast<unsigned char>(name[0])) != 0 &&
           name[1] == ':' &&
           name[2] == '/';
}

[[nodiscard]] size_t afslnk_cut_prefix_length(const std::vector<std::string>& names) {
    if (names.empty()) {
        return 0;
    }

    size_t start_offset = 0;
    if (std::ranges::all_of(names, [](const std::string& name) { return has_windows_drive_prefix(name); })) {
        start_offset = 3;
    }
    if (const auto short_name = std::ranges::find_if(
            names, [=](const auto& name) { return name.size() <= start_offset; });
        short_name != names.end())
        return short_name->size();

    size_t offset = start_offset;
    for (auto end = names.front().find('/', offset);
         end != std::string::npos;
         end = names.front().find('/', offset)) {
        const auto component = std::string_view(names.front()).substr(offset, end - offset);
        const bool matches = std::ranges::all_of(names, [&](const auto& name) {
            const auto other_end = name.find('/', offset);
            return other_end != std::string::npos &&
                std::string_view(name).substr(offset, other_end - offset) == component;
        });
        if (!matches) break;
        offset = end + 1;
    }
    return offset;
}

[[nodiscard]] std::string convert_to_header_macro_name(std::string text) {
    for (char& character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0) {
            character = static_cast<char>(std::toupper(byte));
        } else {
            character = '_';
        }
    }
    return text;
}

} // namespace

AfsContainer AfsContainer::create(uint32_t alignment, bool include_directory_table) {
    AfsContainer container;
    container.m_alignment = alignment == 0 ? DEFAULT_ALIGNMENT : alignment;
    container.m_emit_directory_table = include_directory_table;
    return container;
}

std::expected<AfsContainer, std::string> AfsContainer::create_from_als(
    const std::filesystem::path& als_path,
    uint32_t alignment,
    bool include_directory_table,
    std::optional<std::filesystem::path> source_root
) {
    std::ifstream file(als_path);
    if (!file) {
        return std::unexpected("AFS ALS load failed: could not open file list: " + als_path.string());
    }

    AfsContainer container = create(alignment, include_directory_table);
    const std::filesystem::path als_root = als_path.parent_path();
    const std::filesystem::path source_lookup_root =
        source_root && !source_root->empty()
            ? normalize_generic_tool_path(source_root->string())
            : std::filesystem::path{};
    std::filesystem::path current_header_source_dir;
    std::string current_header_source_dir_text;
    std::filesystem::path current_lookup_dir = source_lookup_root;
    uint32_t next_file_id = 0;
    std::string raw_line;
    size_t line_number = 0;

    while (std::getline(file, raw_line)) {
        ++line_number;
        const std::string line = trim_ascii(raw_line);
        if (line.empty()) {
            continue;
        }

        if (starts_with_case_insensitive(line, ":DIR=")) {
            const std::string argument = trim_ascii(std::string_view(line).substr(5));
            if (argument.size() < 2 || argument.front() != '(' || argument.back() != ')') {
                return std::unexpected(
                    "AFS ALS parse failed at line " + std::to_string(line_number) +
                    ": malformed :DIR= command"
                );
            }

            const std::string directory_text =
                strip_optional_quotes(std::string_view(argument).substr(1, argument.size() - 2));
            current_header_source_dir_text = directory_text;
            current_header_source_dir =
                directory_text.empty() ? std::filesystem::path{} : normalize_generic_tool_path(directory_text);
            if (current_header_source_dir.empty()) {
                current_lookup_dir = source_lookup_root;
            } else if (current_header_source_dir.is_absolute() || source_lookup_root.empty()) {
                current_lookup_dir = current_header_source_dir;
            } else {
                current_lookup_dir = (source_lookup_root / current_header_source_dir).lexically_normal();
            }
            continue;
        }

        if (line.starts_with(":(")) {
            if (line.back() != ')') {
                return std::unexpected(
                    "AFS ALS parse failed at line " + std::to_string(line_number) +
                    ": malformed file ID command"
                );
            }

            const std::string file_id_text =
                trim_ascii(std::string_view(line).substr(2, line.size() - 3));
            auto file_id = parse_u32_text(file_id_text, "AFS ALS parse failed at line " + std::to_string(line_number));
            if (!file_id) {
                return std::unexpected(file_id.error());
            }
            if (*file_id < next_file_id) {
                return std::unexpected(
                    "AFS ALS parse failed at line " + std::to_string(line_number) +
                    ": Illegal ID number"
                );
            }
            next_file_id = *file_id;
            continue;
        }

        if (line.starts_with(':')) {
            return std::unexpected(
                "AFS ALS parse failed at line " + std::to_string(line_number) +
                ": unsupported command"
            );
        }

        const std::string source_text = strip_optional_quotes(line);
        if (source_text.empty()) {
            continue;
        }

        const std::filesystem::path listed_path = normalize_generic_tool_path(source_text);
        const bool is_absolute_path = listed_path.is_absolute();
        const std::string display_path =
            is_absolute_path || current_header_source_dir_text.empty()
                ? source_text
                : join_tool_display_path(current_header_source_dir_text, source_text);
        const std::string normalized_display_path = normalize_windows_display_path(display_path);
        const std::filesystem::path resolved_path =
            is_absolute_path
                ? listed_path
                : current_lookup_dir.empty()
                    ? (als_root / listed_path).lexically_normal()
                    : current_lookup_dir.is_absolute()
                        ? (current_lookup_dir / listed_path).lexically_normal()
                        : (als_root / current_lookup_dir / listed_path).lexically_normal();

        auto bytes = io::read_file_bytes(resolved_path, "AFS ALS entry load failed");
        if (!bytes) {
            return std::unexpected(bytes.error());
        }

        container.add_file_at_id(next_file_id, *bytes, resolved_path.filename().string());
        auto set_result = container.set_header_source_name(next_file_id, normalized_display_path);
        if (!set_result) {
            return std::unexpected(set_result.error());
        }
        ++next_file_id;
    }

    if (!file.eof()) {
        return std::unexpected("AFS ALS load failed: could not read file list: " + als_path.string());
    }
    if (container.present_entry_count() == 0) {
        return std::unexpected("AFS ALS file list has no input files");
    }

    return container;
}

std::expected<std::vector<uint8_t>, std::string> AfsContainer::build() {
    if (m_entries.empty()) {
        return std::unexpected("AFS build failed: no entries to build");
    }
    if (present_entry_count() == 0) {
        return std::unexpected("AFS build failed: no populated entries are present");
    }
    if (m_alignment == 0) {
        return std::unexpected("AFS build failed: alignment must be non-zero");
    }
    const uint64_t header_size = 0x10ull + static_cast<uint64_t>(m_entries.size()) * 0x08ull;
    if (header_size > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("AFS build failed: header size exceeds supported range");
    }

    std::vector<std::span<const uint8_t>> payloads(m_entries.size());
    for (size_t index = 0; index < m_entries.size(); ++index) {
        if (!m_entries[index].present) {
            continue;
        }
        auto payload = build_payload(static_cast<uint32_t>(index));
        if (!payload) {
            return std::unexpected(payload.error());
        }
        if (payload->size() > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected("AFS build failed: entry size exceeds supported range");
        }
        payloads[index] = *payload;
    }

    for (size_t index = 0; index < m_entries.size(); ++index) {
        auto& entry = m_entries[index];
        entry.index = static_cast<uint32_t>(index);
        if (!entry.present) {
            entry.offset = 0;
            entry.size = 0;
            entry.type = AfsEntryType::unknown;
            continue;
        }
        entry.size = static_cast<uint32_t>(payloads[index].size());
        entry.type = detail::detect_entry_type(payloads[index], 0, entry.size);
    }

    const bool include_directory_table = m_emit_directory_table;
    bool preserve_layout = !m_source.empty();
    uint32_t original_first_present_offset = detail::first_present_source_offset(m_source);
    const auto requested_first_payload_offset = m_first_payload_offset;
    uint32_t payload_end = 0;
    if (preserve_layout) {
        const auto minimum_first_offset = static_cast<uint32_t>(header_size);
        std::optional<uint32_t> previous_present_end;
        for (const auto& entry : m_entries) {
            if (!entry.present) continue;
            if (original_first_present_offset == 0) {
                original_first_present_offset = entry.offset;
            }
            if (entry.offset < minimum_first_offset) {
                preserve_layout = false;
                break;
            }
            if (previous_present_end && entry.offset < *previous_present_end) {
                preserve_layout = false;
                break;
            }
            previous_present_end = entry.offset + entry.size;
        }

        if (preserve_layout && requested_first_payload_offset.has_value() &&
            original_first_present_offset != *requested_first_payload_offset) {
            preserve_layout = false;
        }

        if (preserve_layout && include_directory_table) {
            if (!m_directory_table_offset || !m_directory_table_size ||
                *m_directory_table_offset < *previous_present_end ||
                *m_directory_table_size < m_entries.size() * detail::directory_entry_size) {
                preserve_layout = false;
            }
        }
    }

    uint32_t final_directory_offset = 0;
    uint32_t directory_size = 0;
    uint64_t total_size = 0;
    if (preserve_layout) {
        final_directory_offset = include_directory_table ? *m_directory_table_offset : 0;
        directory_size = include_directory_table ? *m_directory_table_size : 0;
        total_size = m_source.size();
    } else {
        uint32_t current_offset = align_up(static_cast<uint32_t>(header_size), m_alignment);
        if (original_first_present_offset != 0) {
            current_offset = std::max(current_offset, original_first_present_offset);
        }
        if (requested_first_payload_offset.has_value()) {
            current_offset = std::max(current_offset, align_up(*requested_first_payload_offset, m_alignment));
        }
        uint32_t last_payload_end = current_offset;
        for (size_t index = 0; index < m_entries.size(); ++index) {
            auto& entry = m_entries[index];
            if (!entry.present) continue;
            entry.offset = current_offset;
            last_payload_end = current_offset + entry.size;
            current_offset = align_up(last_payload_end, m_alignment);
        }

        payload_end = last_payload_end;
        if (include_directory_table) {
            final_directory_offset = align_up(payload_end, m_alignment);
            directory_size = static_cast<uint32_t>(m_entries.size() * detail::directory_entry_size);
            total_size = align_up(final_directory_offset + directory_size, m_alignment);
        } else {
            total_size = payload_end;
        }
    }

    if (total_size > std::numeric_limits<size_t>::max()) {
        return std::unexpected("AFS build failed: output size exceeds addressable memory");
    }

    std::vector<uint8_t> output(static_cast<size_t>(total_size), 0);
    std::copy(detail::afs_magic.begin(), detail::afs_magic.end(), output.begin());
    io::write_le<uint32_t>(output.data() + 0x04, static_cast<uint32_t>(m_entries.size()));

    for (size_t index = 0; index < m_entries.size(); ++index) {
        const size_t entry_offset = 0x08u + index * 0x08u;
        io::write_le<uint32_t>(output.data() + entry_offset + 0x00, m_entries[index].offset);
        io::write_le<uint32_t>(output.data() + entry_offset + 0x04, m_entries[index].size);
    }

    const size_t directory_info_offset = 0x08u + m_entries.size() * 0x08u;
    io::write_le<uint32_t>(output.data() + directory_info_offset + 0x00, final_directory_offset);
    io::write_le<uint32_t>(output.data() + directory_info_offset + 0x04, directory_size);

    for (size_t index = 0; index < m_entries.size(); ++index) {
        const auto& entry = m_entries[index];
        if (!entry.present) {
            continue;
        }
        const auto payload = payloads[index];
        std::copy(payload.begin(), payload.end(), output.data() + entry.offset);
    }

    if (include_directory_table) {
        for (size_t index = 0; index < m_entries.size(); ++index) {
            if (!m_entries[index].present) {
                continue;
            }
            const size_t record_offset = static_cast<size_t>(final_directory_offset) + index * detail::directory_entry_size;
            const std::string record_name = detail::directory_record_name(m_entries[index].name);
            if (!record_name.empty()) {
                std::copy(record_name.begin(), record_name.end(), output.data() + record_offset);
            }
            std::copy(
                m_entries[index].directory_metadata.begin(),
                m_entries[index].directory_metadata.end(),
                output.data() + record_offset + detail::directory_name_size
            );
            io::write_le<uint32_t>(
                output.data() + record_offset + detail::directory_name_size + m_entries[index].directory_metadata.size(),
                m_entries[index].size
            );
        }
        m_directory_table_offset = final_directory_offset;
        m_directory_table_size = directory_size;
    } else {
        m_directory_table_offset.reset();
        m_directory_table_size.reset();
    }

    m_owned_source = output;
    m_source = io::SourceView(std::span<const uint8_t>(m_owned_source), {});
    const uint32_t first_payload_offset = detail::first_present_source_offset(m_source);
    if (first_payload_offset != 0) {
        m_first_payload_offset = first_payload_offset;
    } else {
        m_first_payload_offset.reset();
    }

    return output;
}

std::expected<void, std::string> AfsContainer::build_to_file(const std::filesystem::path& output_path) {
    auto built = build();
    if (!built) {
        return std::unexpected(built.error());
    }

    return io::write_file_bytes(output_path, *built, "AFS build failed");
}

std::expected<std::string, std::string> AfsContainer::build_file_id_header(
    std::string_view archive_name,
    std::string_view id_prefix,
    AfsHeaderNameMode name_mode
) const {
    const std::string archive_banner_name = normalize_archive_header_banner_name(archive_name);
    if (archive_banner_name.empty()) {
        return std::unexpected("AFS header build failed: archive name is empty");
    }
    const std::string archive_macro_name = normalize_archive_header_macro_name(archive_name);
    if (archive_macro_name.empty()) {
        return std::unexpected("AFS header build failed: archive name is empty");
    }

    struct HeaderEntry {
        uint32_t file_id = 0;
        std::string display_name;
        std::string macro_source_name;
    };

    std::vector<HeaderEntry> header_entries;
    header_entries.reserve(present_entry_count());
    for (const auto& entry : m_entries) {
        if (!entry.present) {
            continue;
        }
        const std::optional<std::string>& source_name =
            entry.header_source_name && !entry.header_source_name->empty()
                ? entry.header_source_name
                : entry.name;
        if (!source_name || source_name->empty()) {
            return std::unexpected(
                "AFS header build failed: entry " + std::to_string(entry.index) +
                " has no source name for header generation"
            );
        }

        header_entries.push_back(HeaderEntry{
            .file_id = entry.index,
            .display_name = *source_name,
            .macro_source_name = normalize_macro_source_name(*source_name),
        });
    }

    if (header_entries.empty()) {
        return std::unexpected("AFS header build failed: no populated entries are present");
    }

    std::vector<std::string> normalized_names;
    normalized_names.reserve(header_entries.size());
    for (const auto& entry : header_entries) {
        normalized_names.push_back(entry.macro_source_name);
    }

    const size_t cut_offset = [&]() -> size_t {
        switch (name_mode) {
            case AfsHeaderNameMode::filename_only:
                return std::string::npos;
            case AfsHeaderNameMode::cut_overlapping_string:
                return afslnk_cut_prefix_length(normalized_names);
            case AfsHeaderNameMode::full_path:
                return 0;
        }
        return 0;
    }();

    std::ostringstream header;
    header << "/*\n"
           << " * '" << archive_banner_name << "' FILE ID Header\n"
           << " */\n\n";

    for (const auto& entry : header_entries) {
        std::string macro_source_text;
        switch (name_mode) {
            case AfsHeaderNameMode::filename_only: {
                const size_t offset = filename_offset(entry.macro_source_name);
                macro_source_text = entry.macro_source_name.substr(offset);
                break;
            }
            case AfsHeaderNameMode::cut_overlapping_string:
                macro_source_text =
                    entry.macro_source_name.substr(std::min(cut_offset, entry.macro_source_name.size()));
                break;
            case AfsHeaderNameMode::full_path:
                macro_source_text = entry.macro_source_name;
                break;
        }

        const std::string macro_name = convert_to_header_macro_name(std::string(id_prefix) + macro_source_text);
        header << "#define " << macro_name << " (" << entry.file_id << ") /*  "
               << entry.display_name << "  */\n";
    }

    header << "\n/* number of inside files */\n";
    header << "#define " << convert_to_header_macro_name(archive_macro_name)
           << "_NUM_FILES (" << entry_count() << ")\n";

    return header.str();
}

} // namespace cricodecs::afs
