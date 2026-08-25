#include "modules/cvm/cvm_edit.hpp"

#include "cvm_build_script.hpp"
#include "cvm_builder.hpp"
#include "editor/editor_helpers.hpp"
#include "path_text.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace cristudio::modules::cvm {
namespace {

} // namespace

std::vector<TransformDetailRow> detail_rows(const cricodecs::cvm::CvmContainer& cvm) {
    const auto& header = cvm.header();
    const auto& zone = cvm.zone();
    const auto& pv = cvm.primary_volume();
    return {
        {QCoreApplication::translate("Cvm.CvmEdit", "Disc name"), utf8_to_qstring(cvm.disc_name())},
        {QCoreApplication::translate("Cvm.CvmEdit", "Recording date"), utf8_to_qstring(cvm.recording_date_text())},
        {QStringLiteral("Media"), utf8_to_qstring(cvm.media())},
        {QStringLiteral("Scrambled"), cvm.is_scrambled() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("Accessible"), cvm.has_accessible_contents() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("Entries"), QString::number(cvm.entry_count())},
        {QCoreApplication::translate("Cvm.CvmEdit", "ISO offset"), QString::number(cvm.embedded_iso_offset())},
        {QCoreApplication::translate("Cvm.CvmEdit", "ISO size"), QString::number(cvm.embedded_iso_size())},
        {QCoreApplication::translate("Cvm.CvmEdit", "Header flags"), QString::number(header.flags)},
        {QCoreApplication::translate("Cvm.CvmEdit", "Filesystem ID"), utf8_to_qstring(header.filesystem_id)},
        {QCoreApplication::translate("Cvm.CvmEdit", "Maker ID"), utf8_to_qstring(header.maker_id)},
        {QCoreApplication::translate("Cvm.CvmEdit", "Zone sector"), QString::number(zone.zone_sector)},
        {QCoreApplication::translate("Cvm.CvmEdit", "Data sector"), QString::number(zone.data_sector)},
        {QCoreApplication::translate("Cvm.CvmEdit", "System ID"), utf8_to_qstring(pv.system_identifier)},
        {QCoreApplication::translate("Cvm.CvmEdit", "Volume ID"), utf8_to_qstring(pv.volume_identifier)},
        {QCoreApplication::translate("Cvm.CvmEdit", "Volume set"), utf8_to_qstring(pv.volume_set_identifier)},
        {QStringLiteral("Publisher"), utf8_to_qstring(pv.publisher_identifier)},
        {QCoreApplication::translate("Cvm.CvmEdit", "Data preparer"), utf8_to_qstring(pv.data_preparer_identifier)},
        {QStringLiteral("Application"), utf8_to_qstring(pv.application_identifier)}
    };
}

std::expected<void, std::string> extract_all(
    std::span<const uint8_t> bytes,
    const std::filesystem::path& output_dir
) {
    auto archive = cricodecs::cvm::CvmContainer::load(bytes);
    if (!archive) {
        return std::unexpected(archive.error());
    }
    return archive->extract(output_dir);
}

std::expected<std::vector<uint8_t>, std::string> save_session_bytes(
    const cricodecs::cvm::CvmContainer& cvm
) {
    return cvm.save();
}

std::expected<ImportedScript, std::string> import_build_script(
    const std::filesystem::path& script_path
) {
    auto script = cricodecs::cvm::CvmBuildScript::load(script_path);
    if (!script) {
        return std::unexpected(script.error());
    }

    cricodecs::cvm::CvmBuilder builder;
    auto built = builder.build(*script);
    if (!built) {
        return std::unexpected(built.error());
    }

    auto built_copy = *built;
    auto loaded = cricodecs::cvm::CvmContainer::load(std::move(built_copy));
    if (!loaded) {
        return std::unexpected(loaded.error());
    }

    return ImportedScript{
        .bytes = std::move(*built),
        .container = std::move(*loaded)
    };
}

std::expected<void, std::string> export_build_script(
    const cricodecs::cvm::CvmContainer& cvm,
    const std::filesystem::path& script_path
) {
    return cvm.export_script_file(script_path);
}

std::expected<void, std::string> set_metadata_options(
    cricodecs::cvm::CvmContainer& cvm,
    const MetadataOptions& options
) {
    cvm.set_disc_name(options.disc_name);
    cvm.set_recording_date(options.recording_date);
    auto media_result = cvm.set_media(options.media);
    if (!media_result) {
        return media_result;
    }
    cvm.set_system_identifier(options.system_identifier);
    cvm.set_volume_identifier(options.volume_identifier);
    cvm.set_volume_set_identifier(options.volume_set_identifier);
    cvm.set_publisher_identifier(options.publisher_identifier);
    cvm.set_data_preparer_identifier(options.data_preparer_identifier);
    cvm.set_application_identifier(options.application_identifier);
    return {};
}

std::expected<uint32_t, std::string> add_bytes(
    cricodecs::cvm::CvmContainer& cvm,
    std::span<const uint8_t> bytes,
    const std::filesystem::path& archive_path
) {
    return cvm.add_bytes(bytes, archive_path);
}

std::expected<void, std::string> replace_bytes(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t index,
    std::span<const uint8_t> bytes
) {
    return cvm.replace_bytes(index, bytes);
}

std::expected<void, std::string> remove_file(cricodecs::cvm::CvmContainer& cvm, uint32_t index) {
    return cvm.remove(index);
}

std::expected<void, std::string> move_file(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t from_index,
    uint32_t to_index
) {
    return cvm.move_file(from_index, to_index);
}

std::expected<void, std::string> rename_file(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t index,
    const std::filesystem::path& archive_path
) {
    return cvm.rename(index, archive_path);
}

QString entry_preview(
    const cricodecs::cvm::CvmContainer& cvm,
    uint32_t index,
    std::span<const uint8_t> bytes
) {
    if (index >= cvm.entry_count()) {
        return compact_hex_preview(bytes, "Cvm.CvmEdit");
    }

    const auto& entry = cvm.entry(index);
    const auto& header = cvm.header();
    const auto& zone = cvm.zone();
    const auto& pv = cvm.primary_volume();
    QStringList lines;
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Entry index: %1").arg(entry.index));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Archive path: %1").arg(path_to_qstring(entry.path)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Extent sector: %1").arg(entry.extent_sector));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Declared size: %1").arg(entry.size));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Payload bytes: %1").arg(static_cast<qulonglong>(bytes.size())));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "CVM session"));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Disc name: %1").arg(utf8_to_qstring(cvm.disc_name())));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Recording date: %1").arg(utf8_to_qstring(cvm.recording_date_text())));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Scrambled: %1").arg(cvm.is_scrambled() ? QStringLiteral("yes") : QStringLiteral("no")));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Accessible contents: %1").arg(cvm.has_accessible_contents() ? QStringLiteral("yes") : QStringLiteral("no")));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Entry count: %1").arg(cvm.entry_count()));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Embedded ISO offset: %1").arg(static_cast<qulonglong>(cvm.embedded_iso_offset())));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Embedded ISO size: %1").arg(static_cast<qulonglong>(cvm.embedded_iso_size())));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Embedded ISO sectors: %1").arg(cvm.embedded_iso_sector_count()));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "CVMH header"));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Chunk length: %1").arg(static_cast<qulonglong>(header.chunk_length)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Total size: %1").arg(static_cast<qulonglong>(header.total_size)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Recording date bytes: %1").arg(bytes_to_hex(std::span<const uint8_t>(header.recording_date.data(), header.recording_date.size()))));
    const auto flags_hex = QString::number(header.flags, 16).toUpper();
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Flags: 0x%1 (%2)").arg(flags_hex).arg(header.flags));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Filesystem ID: %1").arg(utf8_to_qstring(header.filesystem_id)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Maker ID: %1").arg(utf8_to_qstring(header.maker_id)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Sector table entries: %1").arg(cvm.sector_table().size()));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Zone sector index: %1").arg(header.zone_sector_index));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "ISO start sector: %1").arg(header.iso_start_sector));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "ZONE layout"));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Chunk length: %1").arg(static_cast<qulonglong>(zone.chunk_length)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Zone sector: %1").arg(zone.zone_sector));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Sector length 1: %1").arg(zone.sector_length_1));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Sector length 2: %1").arg(zone.sector_length_2));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Data sector: %1").arg(zone.data_sector));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Data length: %1").arg(static_cast<qulonglong>(zone.data_length)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "ISO sector: %1").arg(zone.iso_sector));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "ISO length: %1").arg(static_cast<qulonglong>(zone.iso_length)));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Primary volume"));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "System identifier: %1").arg(utf8_to_qstring(pv.system_identifier)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Volume identifier: %1").arg(utf8_to_qstring(pv.volume_identifier)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Volume set identifier: %1").arg(utf8_to_qstring(pv.volume_set_identifier)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Publisher identifier: %1").arg(utf8_to_qstring(pv.publisher_identifier)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Data preparer identifier: %1").arg(utf8_to_qstring(pv.data_preparer_identifier)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Application identifier: %1").arg(utf8_to_qstring(pv.application_identifier)));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Volume space size: %1").arg(pv.volume_space_size));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Logical block size: %1").arg(pv.logical_block_size));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QCoreApplication::translate("Cvm.CvmEdit", "Hex preview"));
    lines.push_back(compact_hex_preview(bytes, "Cvm.CvmEdit"));
    return lines.join(QLatin1Char('\n'));
}

} // namespace cristudio::modules::cvm
