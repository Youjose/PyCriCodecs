/**
 * @file cvm_build_script.cpp
 * @brief CVM/ROFS build-script parser and formatter.
 *
 * Script behavior follows the reviewed ROFSBLD script surface.
 */

#include "cvm_build_script.hpp"

#include <algorithm>
#include <array>
#include <flat_map>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

#include "cvm_path.hpp"
#include "../utilities/string.hpp"

namespace cricodecs::cvm {

namespace {

constexpr size_t rofs_archive_name_max = 31;

struct PendingFile {
    std::filesystem::path archive_path;
    std::optional<std::filesystem::path> source_path;
};

enum class ScriptBlock {
    zone,
    volume,
    primary_volume,
    directory,
    file,
    file_source,
};

[[nodiscard]] std::expected<std::string, std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return std::unexpected("CVM build script load failed: could not open script: " + path.string());
    }

    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    );
}

[[nodiscard]] std::string unquote(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return std::string(value.substr(1, value.size() - 2));
    }
    return std::string(value);
}

[[nodiscard]] std::string quote(std::string_view value) {
    return "\"" + std::string(value) + "\"";
}

struct ScriptDirectoryNode {
    std::map<std::string, ScriptDirectoryNode, std::less<>> directories;
    std::map<std::string, std::filesystem::path, std::less<>> files;
};

void add_export_file(
    ScriptDirectoryNode& root,
    const std::filesystem::path& archive_path,
    const std::filesystem::path& source_path
) {
    const std::filesystem::path normalized_path = normalize_archive_path(archive_path);
    ScriptDirectoryNode* current = &root;
    for (auto it = normalized_path.begin(); it != normalized_path.end(); ++it) {
        const std::string component = it->generic_string();
        if (std::next(it) == normalized_path.end()) {
            current->files.emplace(component, source_path);
            break;
        }
        current = &current->directories[component];
    }
}

void emit_directory_tree(std::ostringstream& output, const ScriptDirectoryNode& node, size_t depth) {
    const std::string indent(depth, '\t');
    for (const auto& [name, child] : node.directories) {
        output << indent << "Directory " << name << "\n";
        emit_directory_tree(output, child, depth + 1);
        output << indent << "EndDirectory\n";
    }
    for (const auto& [name, source_path] : node.files) {
        output << indent << "File " << name << "\n";
        output << indent << "\tFileSource " << quote(source_path.generic_string()) << "\n";
        output << indent << "\tEndFileSource\n";
        output << indent << "EndFile\n";
    }
}

[[nodiscard]] bool has_path_separator(std::string_view value) {
    return value.find('/') != std::string_view::npos || value.find('\\') != std::string_view::npos;
}

[[nodiscard]] std::expected<std::string, std::string> validate_archive_name(
    std::string_view raw_name,
    std::string_view keyword,
    size_t line_number
) {
    const std::string name = util::trim_ascii(unquote(raw_name));
    if (name.empty()) {
        return std::unexpected(
            std::string(keyword) + " line is missing a name in CVM build script at line " + std::to_string(line_number)
        );
    }
    if (name == "." || name == ".." || has_path_separator(name)) {
        return std::unexpected(
            std::string(keyword) + " name must be a single ROFS path component at line " + std::to_string(line_number) +
            ": " + name
        );
    }
    if (name.size() > rofs_archive_name_max) {
        return std::unexpected(
            std::string(keyword) + " name exceeds the ROFS 31-character limit at line " + std::to_string(line_number) +
            ": " + name
        );
    }
    return name;
}

[[nodiscard]] std::string expand_defines(std::string value, const std::flat_map<std::string, std::string>& defines) {
    bool changed = true;
    while (changed) {
        changed = false;
        size_t open = value.find('[');
        while (open != std::string::npos) {
            const size_t close = value.find(']', open + 1);
            if (close == std::string::npos) {
                break;
            }

            const std::string key = value.substr(open + 1, close - open - 1);
            const auto found = defines.find(key);
            if (found == defines.end()) {
                open = value.find('[', close + 1);
                continue;
            }

            value.replace(open, close - open + 1, found->second);
            changed = true;
            open = value.find('[', open + found->second.size());
        }
    }
    return value;
}

[[nodiscard]] std::expected<void, std::string> finalize_pending_file(
    std::optional<PendingFile>& pending_file,
    std::vector<CvmBuildScriptFile>& files,
    size_t line_number
) {
    if (!pending_file.has_value()) {
        return {};
    }
    if (!pending_file->source_path.has_value()) {
        return std::unexpected("CVM build script file is missing FileSource before EndFile at line " + std::to_string(line_number));
    }

    files.push_back({
        .index = static_cast<uint32_t>(files.size()),
        .archive_path = pending_file->archive_path,
        .source_path = *pending_file->source_path,
    });
    pending_file.reset();
    return {};
}

[[nodiscard]] std::string block_name(ScriptBlock block) {
    switch (block) {
        case ScriptBlock::zone: return "Zone";
        case ScriptBlock::volume: return "Volume";
        case ScriptBlock::primary_volume: return "PrimaryVolume";
        case ScriptBlock::directory: return "Directory";
        case ScriptBlock::file: return "File";
        case ScriptBlock::file_source: return "FileSource";
    }
    return "Unknown";
}

[[nodiscard]] bool has_enclosing_block(
    const std::vector<ScriptBlock>& block_stack,
    ScriptBlock block
) {
    return std::find(block_stack.begin(), block_stack.end(), block) != block_stack.end();
}

[[nodiscard]] bool current_block_is(
    const std::vector<ScriptBlock>& block_stack,
    ScriptBlock block
) {
    return !block_stack.empty() && block_stack.back() == block;
}

[[nodiscard]] std::expected<void, std::string> require_enclosing_block(
    const std::vector<ScriptBlock>& block_stack,
    ScriptBlock block,
    std::string_view keyword,
    size_t line_number
) {
    if (!has_enclosing_block(block_stack, block)) {
        return std::unexpected(
            std::string(keyword) + " is only supported inside " + block_name(block) +
            " in CVM build script at line " + std::to_string(line_number)
        );
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> require_top_level_directive(
    const std::vector<ScriptBlock>& block_stack,
    bool has_pending_file,
    std::string_view keyword,
    size_t line_number
) {
    if (!block_stack.empty() || has_pending_file) {
        return std::unexpected(
            std::string(keyword) + " must appear at the top level in CVM build script at line " +
            std::to_string(line_number)
        );
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> push_block(
    std::vector<ScriptBlock>& block_stack,
    ScriptBlock block,
    std::optional<ScriptBlock> required_parent,
    size_t line_number
) {
    if (required_parent.has_value() && !current_block_is(block_stack, *required_parent)) {
        const std::string parent_name = block_name(*required_parent);
        return std::unexpected(
            block_name(block) + " must appear inside " + parent_name +
            " in CVM build script at line " + std::to_string(line_number)
        );
    }
    block_stack.push_back(block);
    return {};
}

[[nodiscard]] std::expected<void, std::string> pop_block(
    std::vector<ScriptBlock>& block_stack,
    ScriptBlock block,
    size_t line_number
) {
    if (!current_block_is(block_stack, block)) {
        return std::unexpected(
            "End" + block_name(block) + " without matching " + block_name(block) +
            " in CVM build script at line " + std::to_string(line_number)
        );
    }
    block_stack.pop_back();
    return {};
}

[[nodiscard]] std::expected<void, std::string> validate_supported_rofs_subset(const CvmBuildScript& script) {
    if (script.disc_name().empty()) {
        return std::unexpected("CVM build script is missing a Disc directive");
    }

    const std::filesystem::path disc_path = script.disc_name();
    const std::string extension = util::uppercase_ascii(disc_path.extension().string());
    if (extension != ".CVM") {
        return std::unexpected(
            "CVM build script currently supports only ROFS Disc .cvm outputs: " + script.disc_name()
        );
    }

    if (!script.media().empty() &&
        !util::equals_ascii_case_insensitive(script.media(), "CD") &&
        !util::equals_ascii_case_insensitive(script.media(), "DVD")) {
        return std::unexpected(
            "CVM build script supports only official ROFS Media CD or Media DVD values: " + script.media()
        );
    }

    return {};
}

} // namespace

std::string format_cvm_build_script(const CvmBuildScriptExport& script) {
    std::ostringstream output;
    if (script.define_root.has_value()) {
        output << "Define " << script.define_root->first << " " << quote(script.define_root->second.generic_string()) << "\n";
    }
    output << "Disc " << quote(script.disc_name) << "\n";
    if (!script.recording_date.empty()) {
        output << "RecordingDate " << script.recording_date << "\n";
    }
    if (!script.media.empty()) {
        output << "Media " << script.media << "\n";
    }
    output << "Zone\n";
    output << "\tVolume\n";
    output << "\tPrimaryVolume\n";
    if (!script.system_identifier.empty()) {
        output << "\tSystemIdentifier            " << quote(script.system_identifier) << "\n";
    }
    if (!script.volume_identifier.empty()) {
        output << "\tVolumeIdentifier            " << quote(script.volume_identifier) << "\n";
    }
    if (!script.volume_set_identifier.empty()) {
        output << "\tVolumeSetIdentifier         " << quote(script.volume_set_identifier) << "\n";
    }
    if (!script.publisher_identifier.empty()) {
        output << "\tPublisherIdentifier         " << quote(script.publisher_identifier) << "\n";
    }
    if (!script.data_preparer_identifier.empty()) {
        output << "\tDataPreparerIdentifier      " << quote(script.data_preparer_identifier) << "\n";
    }
    if (!script.application_identifier.empty()) {
        output << "\tApplicationIdentifier       " << quote(script.application_identifier) << "\n";
    }
    output << "\tEndPrimaryVolume\n";
    output << "\tEndVolume\n";

    ScriptDirectoryNode tree;
    for (const auto& file : script.files) {
        add_export_file(tree, file.archive_path, file.source_path);
    }
    emit_directory_tree(output, tree, 1);
    output << "EndZone\n";
    output << "EndDisc\n";
    return output.str();
}

std::expected<CvmBuildScript, std::string> CvmBuildScript::parse(
    std::string_view script_text,
    const std::filesystem::path& script_directory
) {
    CvmBuildScript script;
    std::flat_map<std::string, std::string> defines;
    std::vector<ScriptBlock> block_stack;
    std::vector<std::filesystem::path> directory_stack;
    std::optional<PendingFile> pending_file;
    bool saw_disc = false;
    bool saw_end_disc = false;
    bool saw_zone = false;
    static constexpr std::array primary_volume_fields{
        std::pair<std::string_view, std::string CvmBuildScript::*>{"SystemIdentifier", &CvmBuildScript::m_system_identifier},
        std::pair<std::string_view, std::string CvmBuildScript::*>{"VolumeIdentifier", &CvmBuildScript::m_volume_identifier},
        std::pair<std::string_view, std::string CvmBuildScript::*>{"VolumeSetIdentifier", &CvmBuildScript::m_volume_set_identifier},
        std::pair<std::string_view, std::string CvmBuildScript::*>{"PublisherIdentifier", &CvmBuildScript::m_publisher_identifier},
        std::pair<std::string_view, std::string CvmBuildScript::*>{"DataPreparerIdentifier", &CvmBuildScript::m_data_preparer_identifier},
        std::pair<std::string_view, std::string CvmBuildScript::*>{"ApplicationIdentifier", &CvmBuildScript::m_application_identifier},
    };

    std::istringstream input{std::string(script_text)};
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::string_view raw_line = line;
        const size_t comment_start = raw_line.find(';');
        if (comment_start != std::string_view::npos) {
            raw_line = raw_line.substr(0, comment_start);
        }

        std::string trimmed = util::trim_ascii(raw_line);
        if (trimmed.empty()) {
            continue;
        }

        std::istringstream line_stream(trimmed);
        std::string keyword;
        line_stream >> keyword;

        if (keyword == "Define") {
            auto top_level = require_top_level_directive(block_stack, pending_file.has_value(), keyword, line_number);
            if (!top_level) {
                return std::unexpected(top_level.error());
            }
            if (saw_disc) {
                return std::unexpected(
                    "Define must appear before Disc in CVM build script at line " + std::to_string(line_number)
                );
            }

            std::string name;
            std::string value;
            line_stream >> name;
            std::getline(line_stream, value);
            value = expand_defines(util::trim_ascii(unquote(util::trim_ascii(value))), defines);
            if (name.empty() || value.empty()) {
                return std::unexpected("CVM build script parse failed: invalid Define line at line " + std::to_string(line_number));
            }
            defines[name] = value;
            continue;
        }

        std::string remainder;
        std::getline(line_stream, remainder);
            remainder = util::trim_ascii(remainder);

        if (keyword == "Disc") {
            if (saw_disc) {
                return std::unexpected("CVM build script parse failed: multiple Disc directives are not supported at line " + std::to_string(line_number));
            }
            if (!block_stack.empty() || pending_file.has_value()) {
                return std::unexpected("CVM build script parse failed: Disc must be the top-level opening directive at line " + std::to_string(line_number));
            }
            script.m_disc_name = unquote(remainder);
            saw_disc = true;
        } else if (keyword == "RecordingDate") {
            auto top_level = require_top_level_directive(block_stack, pending_file.has_value(), keyword, line_number);
            if (!top_level) {
                return std::unexpected(top_level.error());
            }
            if (!saw_disc || saw_zone) {
                return std::unexpected(
                    "RecordingDate must appear after Disc and before Zone in CVM build script at line " +
                    std::to_string(line_number)
                );
            }
            script.m_recording_date = remainder;
        } else if (keyword == "Media") {
            auto top_level = require_top_level_directive(block_stack, pending_file.has_value(), keyword, line_number);
            if (!top_level) {
                return std::unexpected(top_level.error());
            }
            if (!saw_disc || saw_zone) {
                return std::unexpected(
                    "Media must appear after Disc and before Zone in CVM build script at line " +
                    std::to_string(line_number)
                );
            }
            script.m_media = remainder;
        } else if (keyword == "Zone") {
            saw_zone = true;
            auto pushed = push_block(block_stack, ScriptBlock::zone, std::nullopt, line_number);
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
        } else if (keyword == "EndZone") {
            auto popped = pop_block(block_stack, ScriptBlock::zone, line_number);
            if (!popped) {
                return std::unexpected(popped.error());
            }
        } else if (keyword == "Volume") {
            auto pushed = push_block(block_stack, ScriptBlock::volume, ScriptBlock::zone, line_number);
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
        } else if (keyword == "EndVolume") {
            auto popped = pop_block(block_stack, ScriptBlock::volume, line_number);
            if (!popped) {
                return std::unexpected(popped.error());
            }
        } else if (keyword == "PrimaryVolume") {
            auto pushed = push_block(block_stack, ScriptBlock::primary_volume, ScriptBlock::volume, line_number);
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
        } else if (keyword == "EndPrimaryVolume") {
            auto popped = pop_block(block_stack, ScriptBlock::primary_volume, line_number);
            if (!popped) {
                return std::unexpected(popped.error());
            }
        } else if (const auto field = std::ranges::find_if(primary_volume_fields, [&](const auto& item) {
                       return item.first == keyword;
                   }); field != primary_volume_fields.end()) {
            auto required = require_enclosing_block(block_stack, ScriptBlock::primary_volume, keyword, line_number);
            if (!required) {
                return std::unexpected(required.error());
            }
            script.*(field->second) = unquote(remainder);
        } else if (keyword == "Directory") {
            auto required = require_enclosing_block(block_stack, ScriptBlock::zone, keyword, line_number);
            if (!required) {
                return std::unexpected(required.error());
            }
            if (!block_stack.empty() &&
                block_stack.back() != ScriptBlock::zone &&
                block_stack.back() != ScriptBlock::directory) {
                return std::unexpected(
                    "Directory must appear directly inside Zone or Directory in CVM build script at line " +
                    std::to_string(line_number)
                );
            }
            auto archive_name = validate_archive_name(remainder, "Directory", line_number);
            if (!archive_name) {
                return std::unexpected(archive_name.error());
            }
            auto pushed = push_block(block_stack, ScriptBlock::directory, std::nullopt, line_number);
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
            directory_stack.push_back(std::filesystem::path(*archive_name));
        } else if (keyword == "EndDirectory") {
            auto popped = pop_block(block_stack, ScriptBlock::directory, line_number);
            if (!popped) {
                return std::unexpected(popped.error());
            }
            if (directory_stack.empty()) {
                return std::unexpected("CVM build script parse failed: directory stack underflow at line " + std::to_string(line_number));
            }
            directory_stack.pop_back();
        } else if (keyword == "File") {
            auto required = require_enclosing_block(block_stack, ScriptBlock::zone, keyword, line_number);
            if (!required) {
                return std::unexpected(required.error());
            }
            if (!block_stack.empty() &&
                block_stack.back() != ScriptBlock::zone &&
                block_stack.back() != ScriptBlock::directory) {
                return std::unexpected(
                    "File must appear directly inside Zone or Directory in CVM build script at line " +
                    std::to_string(line_number)
                );
            }
            auto archive_name = validate_archive_name(remainder, "File", line_number);
            if (!archive_name) {
                return std::unexpected(archive_name.error());
            }
            if (pending_file.has_value()) {
                return std::unexpected("CVM build script parse failed: nested File block is not supported at line " + std::to_string(line_number));
            }
            auto pushed = push_block(block_stack, ScriptBlock::file, std::nullopt, line_number);
            if (!pushed) {
                return std::unexpected(pushed.error());
            }

            std::filesystem::path archive_path;
            for (const auto& component : directory_stack) {
                archive_path /= component;
            }
            archive_path /= *archive_name;
            pending_file = PendingFile{
                .archive_path = archive_path,
                .source_path = std::nullopt,
            };
        } else if (keyword == "FileSource") {
            if (!pending_file.has_value()) {
                return std::unexpected("CVM build script parse failed: FileSource without matching File at line " + std::to_string(line_number));
            }
            auto pushed = push_block(block_stack, ScriptBlock::file_source, ScriptBlock::file, line_number);
            if (!pushed) {
                return std::unexpected(pushed.error());
            }

            std::string expanded = expand_defines(unquote(remainder), defines);
            std::filesystem::path resolved = script_directory.empty()
                ? std::filesystem::path(expanded)
                : (script_directory / expanded).lexically_normal();
            pending_file->source_path = resolved;
        } else if (keyword == "EndFileSource") {
            auto popped = pop_block(block_stack, ScriptBlock::file_source, line_number);
            if (!popped) {
                return std::unexpected(popped.error());
            }
        } else if (keyword == "EndFile") {
            if (current_block_is(block_stack, ScriptBlock::file_source)) {
                return std::unexpected("CVM build script parse failed: EndFile requires EndFileSource first at line " + std::to_string(line_number));
            }
            auto popped = pop_block(block_stack, ScriptBlock::file, line_number);
            if (!popped) {
                return std::unexpected(popped.error());
            }
            auto finalized = finalize_pending_file(pending_file, script.m_files, line_number);
            if (!finalized) {
                return std::unexpected(finalized.error());
            }
        } else if (keyword == "EndDisc") {
            if (!saw_disc) {
                return std::unexpected("CVM build script parse failed: EndDisc without matching Disc at line " + std::to_string(line_number));
            }
            if (!block_stack.empty() || pending_file.has_value() || !directory_stack.empty()) {
                return std::unexpected("CVM build script parse failed: EndDisc requires all nested blocks to be closed first at line " + std::to_string(line_number));
            }
            saw_end_disc = true;
        } else {
            return std::unexpected("CVM build script parse failed: unsupported token '" + keyword + "' at line " + std::to_string(line_number));
        }
    }

    if (pending_file.has_value()) {
        auto finalized = finalize_pending_file(pending_file, script.m_files, line_number);
        if (!finalized) {
            return std::unexpected(finalized.error());
        }
    }
    if (!directory_stack.empty()) {
        return std::unexpected("CVM build script ended with an unterminated Directory block");
    }
    if (!block_stack.empty()) {
        return std::unexpected(
            "CVM build script ended with an unterminated " + block_name(block_stack.back()) + " block"
        );
    }
    if (script.m_disc_name.empty()) {
        return std::unexpected("CVM build script is missing a Disc directive");
    }
    if (saw_disc && !saw_end_disc) {
        return std::unexpected("CVM build script is missing EndDisc");
    }
    if (auto validated = validate_supported_rofs_subset(script); !validated) {
        return std::unexpected(validated.error());
    }

    return script;
}

std::expected<CvmBuildScript, std::string> CvmBuildScript::load(const std::filesystem::path& path) {
    auto text = read_text_file(path);
    if (!text) {
        return std::unexpected(text.error());
    }

    auto parsed = parse(*text, path.parent_path());
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    parsed->m_source_path = path;
    return parsed;
}

std::string CvmBuildScript::to_text() const {
    CvmBuildScriptExport script{
        .disc_name = m_disc_name,
        .recording_date = m_recording_date,
        .media = m_media.empty() ? "DVD" : m_media,
        .system_identifier = m_system_identifier.empty() ? "CRI ROFS" : m_system_identifier,
        .volume_identifier = m_volume_identifier,
        .volume_set_identifier = m_volume_set_identifier,
        .publisher_identifier = m_publisher_identifier,
        .data_preparer_identifier = m_data_preparer_identifier,
        .application_identifier = m_application_identifier,
        .define_root = std::nullopt,
        .files = {},
    };
    script.files.reserve(m_files.size());
    for (const auto& file : m_files) {
        script.files.push_back({
            .archive_path = file.archive_path,
            .source_path = file.source_path,
        });
    }
    return format_cvm_build_script(script);
}

} // namespace cricodecs::cvm
