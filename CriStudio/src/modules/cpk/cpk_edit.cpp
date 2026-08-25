#include "shared/i18n.hpp"
#include <QCoreApplication>
#include "editor/editor_helpers.hpp"
#include "modules/cpk/cpk_edit.hpp"
#include "path_text.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <utility>

namespace cristudio::modules::cpk {
namespace {

QString optional_bool_text(const std::optional<bool>& value) {
    if (!value) {
        return QStringLiteral("auto");
    }
    return *value ? QCoreApplication::translate("Cpk.CpkEdit", "force on") : QCoreApplication::translate("Cpk.CpkEdit", "force off");
}

} // namespace

ScratchArchive create_scratch_archive(cricodecs::cpk::CpkPreset preset) {
    cricodecs::cpk::CpkOptions options;
    options.preset = preset;
    return ScratchArchive{
        .container = cricodecs::cpk::Cpk::create(options),
        .document = LoadedDocument{
            .display_name = "NewArchive.cpk",
            .format = cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK archive (scratch)"),
            .file_size = 0,
            .info = {
                {"Source", cristudio::i18n::translate_utf8("Cpk.CpkEdit", "Scratch CPK archive")},
                {"Files", "0"},
                {"Preset", qstring_to_utf8(cpk_preset_name(options.preset))},
                {"Alignment", std::to_string(options.align)}
            },
            .entry_columns = {
                "Index",
                cristudio::i18n::translate_utf8("Cpk.CpkEdit", "Full Path"),
                "Dirname",
                cristudio::i18n::translate_utf8("Cpk.CpkEdit", "Dirname Raw"),
                "Filename",
                cristudio::i18n::translate_utf8("Cpk.CpkEdit", "Filename Raw"),
                "ID",
                cristudio::i18n::translate_utf8("Cpk.CpkEdit", "TOC Index"),
                "Offset",
                cristudio::i18n::translate_utf8("Cpk.CpkEdit", "File Size"),
                cristudio::i18n::translate_utf8("Cpk.CpkEdit", "Extract Size"),
                "Compressed",
                cristudio::i18n::translate_utf8("Cpk.CpkEdit", "Compress On Save"),
                cristudio::i18n::translate_utf8("Cpk.CpkEdit", "User String"),
            },
            .entry_column_types = {
                "integer",
                "path",
                "path",
                "string",
                "string",
                "string",
                "integer",
                "integer",
                "offset",
                "size",
                "size",
                "state",
                "state",
                "string",
            },
            .entries = {}
        }
    };
}

std::vector<TransformDetailRow> detail_rows(const cricodecs::cpk::Cpk& cpk) {
    const auto& options = cpk.options();
    return {
        {QStringLiteral("Files"), QString::number(cpk.file_count())},
        {QStringLiteral("Mode"), cpk_mode_name(cpk.mode())},
        {QStringLiteral("Preset"), cpk_preset_name(cpk.preset())},
        {QCoreApplication::translate("Cpk.CpkEdit", "Declared preset"), cpk.has_declared_preset() ? cpk_preset_name(cpk.declared_preset()) : QStringLiteral("-")},
        {QCoreApplication::translate("Cpk.CpkEdit", "Option preset"), cpk_preset_name(options.preset)},
        {QStringLiteral("Alignment"), QString::number(cpk.alignment())},
        {QCoreApplication::translate("Cpk.CpkEdit", "Content offset"), QString::number(cpk.content_offset())},
        {QStringLiteral("TOC"), cpk.has_toc() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("ITOC"), cpk.has_itoc() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("GTOC"), cpk.has_gtoc() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("ETOC"), cpk.has_etoc() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QCoreApplication::translate("Cpk.CpkEdit", "Override TOC"), optional_bool_text(options.enable_toc)},
        {QCoreApplication::translate("Cpk.CpkEdit", "Override ITOC"), optional_bool_text(options.enable_itoc)},
        {QCoreApplication::translate("Cpk.CpkEdit", "Override GTOC"), optional_bool_text(options.enable_gtoc)},
        {QCoreApplication::translate("Cpk.CpkEdit", "Override ETOC"), optional_bool_text(options.enable_etoc)},
        {QStringLiteral("CRC"), options.enable_crc ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("Encoding"), options.encoding.encoding ? QString::fromStdString(*options.encoding.encoding) : QStringLiteral("auto")},
        {QStringLiteral("Comment"), QString::fromStdString(options.comment)},
        {QStringLiteral("TVER"), QString::fromStdString(options.tver)},
        {QCoreApplication::translate("Cpk.CpkEdit", "ETOC LocalDir"), QString::fromStdString(options.etoc_local_dir)}
    };
}

void add_bytes(
    cricodecs::cpk::Cpk& cpk,
    std::span<const uint8_t> bytes,
    const std::string& archive_path
) {
    cpk.add_bytes(bytes, archive_path);
}

std::expected<size_t, std::string> add_files(
    cricodecs::cpk::Cpk& cpk,
    std::span<const AddFileSource> files
) {
    for (const auto& file : files) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(file.local_path, error)) {
            if (error) {
                return std::unexpected(
                    cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK add failed: could not inspect input file '") + file.local_path.string() + "': " +
                    error.message());
            }
            return std::unexpected(
                cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK add failed: input is not a regular file: ") + file.local_path.string());
        }
        if (file.archive_path.empty()) {
            return std::unexpected(cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK add failed: archive path is empty"));
        }
    }

    for (const auto& file : files) {
        cpk.add_file(file.local_path, file.archive_path);
    }
    return files.size();
}

std::expected<size_t, std::string> add_directory(
    cricodecs::cpk::Cpk& cpk,
    const std::filesystem::path& root
) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK directory add failed: input is not a directory: ") + root.string());
    }

    std::vector<AddFileSource> files;
    std::filesystem::recursive_directory_iterator iterator(root, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        return std::unexpected(cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK directory add failed: ") + error.message());
    }
    while (iterator != end) {
        if (!iterator->is_regular_file(error)) {
            if (error) {
                return std::unexpected(cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK directory add failed: ") + error.message());
            }
            iterator.increment(error);
            if (error) {
                return std::unexpected(cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK directory add failed: ") + error.message());
            }
            continue;
        }
        const auto relative = iterator->path().lexically_relative(root);
        files.push_back({
            .local_path = iterator->path(),
            .archive_path = relative.generic_string(),
        });
        iterator.increment(error);
        if (error) {
            return std::unexpected(cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK directory add failed: ") + error.message());
        }
    }

    std::ranges::sort(files, {}, [](const AddFileSource& file) { return file.archive_path; });
    return add_files(cpk, files);
}

std::expected<std::vector<uint8_t>, std::string> build_session_bytes(
    cricodecs::cpk::Cpk& cpk,
    bool obfuscate_utf
) {
    return obfuscate_utf ? cpk.encrypt() : cpk.decrypt();
}

void set_options(cricodecs::cpk::Cpk& cpk, cricodecs::cpk::CpkOptions options) {
    cpk.set_options(std::move(options));
}

std::expected<void, std::string> replace_bytes(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    std::span<const uint8_t> bytes
) {
    return cpk.replace_bytes(index, bytes);
}

std::expected<void, std::string> remove_file(cricodecs::cpk::Cpk& cpk, size_t index) {
    return cpk.remove(index);
}

std::expected<void, std::string> move_file(
    cricodecs::cpk::Cpk& cpk,
    size_t from_index,
    size_t to_index
) {
    return cpk.move_file(from_index, to_index);
}

std::expected<void, std::string> rename_file(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    const std::string& archive_path
) {
    return cpk.rename(index, archive_path);
}

std::expected<void, std::string> set_dirname(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    const std::string& dirname
) {
    return cpk.set_dirname(index, dirname);
}

std::expected<void, std::string> set_filename(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    const std::string& filename
) {
    return cpk.set_filename(index, filename);
}

std::expected<void, std::string> set_entry_id(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    uint32_t id
) {
    auto* entry = cpk.try_file(index);
    if (entry == nullptr) {
        return std::unexpected(cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK entry index is out of range"));
    }
    entry->id = id;
    return {};
}

std::expected<void, std::string> set_request_compress(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    bool request_compress
) {
    return cpk.set_request_compress(index, request_compress);
}

void set_all_request_compress(cricodecs::cpk::Cpk& cpk, bool request_compress) {
    cpk.set_all_request_compress(request_compress);
}

std::expected<void, std::string> set_user_string(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    std::string value
) {
    auto* entry = cpk.try_file(index);
    if (entry == nullptr) {
        return std::unexpected(cristudio::i18n::translate_utf8("Cpk.CpkEdit", "CPK entry index is out of range"));
    }
    entry->user_string = std::move(value);
    return {};
}

} // namespace cristudio::modules::cpk
