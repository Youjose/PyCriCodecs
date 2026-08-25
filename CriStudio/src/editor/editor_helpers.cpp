#include "editor/editor_helpers.hpp"

#include "editor_workspace.hpp"
#include "modules/adx/adx_edit.hpp"
#include "modules/aix/aix_edit.hpp"
#include "modules/cvm/cvm_edit.hpp"
#include "modules/hca/hca_edit.hpp"
#include "path_text.hpp"

#include "io_reader.hpp"
#include "aax_container.hpp"
#include "acb_container.hpp"
#include "acx_container.hpp"
#include "afs_container.hpp"
#include "awb_container.hpp"
#include "cpk_container.hpp"
#include "csb_container.hpp"
#include "cvm_container.hpp"
#include "sfd_container.hpp"
#include "usm_container.hpp"

#include <QCoreApplication>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <limits>
#include <system_error>
#include <utility>

namespace cristudio {

void push_job_log(const std::shared_ptr<BuildJobLog>& log, QString message) {
    if (!log) {
        return;
    }
    std::lock_guard lock(log->mutex);
    log->pending.push_back(std::move(message));
}

QStringList take_job_logs(const std::shared_ptr<BuildJobLog>& log) {
    if (!log) {
        return {};
    }
    std::lock_guard lock(log->mutex);
    auto pending = std::move(log->pending);
    log->pending.clear();
    return pending;
}

QString editor_label(std::string_view text, QString fallback) {
    auto label = utf8_to_qstring(std::string(text)).trimmed();
    return label.isEmpty() ? std::move(fallback) : label;
}

QString editor_format_id(const EditorOpenRequest& request) {
    if (!request.detected_format.empty()) {
        return editor_label(request.detected_format, {});
    }
    if (request.document) {
        return editor_label(
            request.document->loader_tag.empty()
                ? request.document->format
                : request.document->loader_tag,
            {}
        );
    }
    if (request.entry) {
        return editor_label(
            request.entry->source_format.empty()
                ? request.entry->type
                : request.entry->source_format,
            {}
        );
    }
    return {};
}

QString editor_format_label(const EditorOpenRequest& request) {
    switch (request.scratch_kind) {
    case EditorOpenRequest::ScratchKind::Utf:
        return QCoreApplication::translate("Editor.EditorHelpers", "UTF table");
    case EditorOpenRequest::ScratchKind::Afs:
        return QCoreApplication::translate("Editor.EditorHelpers", "AFS archive");
    case EditorOpenRequest::ScratchKind::Awb:
        return QCoreApplication::translate("Editor.EditorHelpers", "AWB/AFS2 archive");
    case EditorOpenRequest::ScratchKind::Acx:
        return QCoreApplication::translate("Editor.EditorHelpers", "ACX archive");
    case EditorOpenRequest::ScratchKind::Cpk:
        return QCoreApplication::translate("Editor.EditorHelpers", "CPK archive");
    case EditorOpenRequest::ScratchKind::AudioEncode:
        return QCoreApplication::translate("Editor.EditorHelpers", "Audio encode job");
    case EditorOpenRequest::ScratchKind::MediaBuild:
        return QCoreApplication::translate("Editor.EditorHelpers", "USM/SFD build job");
    case EditorOpenRequest::ScratchKind::AaxBuild:
        return QCoreApplication::translate("Editor.EditorHelpers", "AAX ADX build job");
    case EditorOpenRequest::ScratchKind::AixBuild:
        return QCoreApplication::translate("Editor.EditorHelpers", "AIX ADX build job");
    case EditorOpenRequest::ScratchKind::CsbBuild:
        return QCoreApplication::translate("Editor.EditorHelpers", "CSB folder build job");
    case EditorOpenRequest::ScratchKind::CvmScript:
        return QCoreApplication::translate("Editor.EditorHelpers", "CVM build script");
    case EditorOpenRequest::ScratchKind::CvmDirectory:
        return QCoreApplication::translate("Editor.EditorHelpers", "CVM directory");
    case EditorOpenRequest::ScratchKind::None:
        break;
    }
    if (request.document) {
        return editor_label(
            localized_document_format(*request.document),
            QCoreApplication::translate("Editor.EditorHelpers", "Unknown format")
        );
    }
    if (!request.detected_format.empty()) {
        return editor_label(request.detected_format, QCoreApplication::translate("Editor.EditorHelpers", "Unknown format"));
    }
    if (request.entry) {
        return editor_label(request.entry->type.empty() ? request.entry->source_format : request.entry->type, QCoreApplication::translate("Editor.EditorHelpers", "Archive entry"));
    }
    return QCoreApplication::translate("Editor.EditorHelpers", "Scratch document");
}

QString safe_output_name(QString name, QString fallback_suffix) {
    name = name.trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("editor-output");
    }
    for (auto& ch : name) {
        if (ch == QLatin1Char('/') || ch == QLatin1Char('\\') || ch == QLatin1Char(':') || ch == QLatin1Char('*') ||
            ch == QLatin1Char('?') || ch == QLatin1Char('"') || ch == QLatin1Char('<') || ch == QLatin1Char('>') ||
            ch == QLatin1Char('|')) {
            ch = QLatin1Char('_');
        }
    }
    if (!fallback_suffix.isEmpty() && !name.endsWith(fallback_suffix, Qt::CaseInsensitive)) {
        name += fallback_suffix;
    }
    return name;
}

QString ensure_output_suffix(QString text, QString suffix) {
    text = text.trimmed();
    if (text.isEmpty() || suffix.isEmpty() || text.endsWith(suffix, Qt::CaseInsensitive)) {
        return text;
    }
    const QFileInfo info(text);
    const auto file_name = safe_output_name(info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName(), suffix);
    if (info.path().isEmpty() || info.path() == QStringLiteral(".")) {
        return file_name;
    }
    return info.dir().filePath(file_name);
}

QString build_output_base_name(const QString& title) {
    auto base = title.trimmed();
    const auto dot = base.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        base.truncate(dot);
    }
    return base.isEmpty() ? QStringLiteral("build") : base;
}

QString bytes_to_hex(std::span<const uint8_t> bytes) {
    if (bytes.empty()) {
        return {};
    }
    const auto view = QByteArray::fromRawData(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<qsizetype>(bytes.size()));
    return QString::fromLatin1(view.toHex().toUpper());
}

QString compact_hex_preview(
    std::span<const uint8_t> bytes,
    const char* translation_context,
    size_t max_bytes
) {
    const auto count = std::min(bytes.size(), max_bytes);
    QString out;
    out.reserve(static_cast<qsizetype>(count * 3 + 64));
    for (size_t index = 0; index < count; ++index) {
        if (index != 0) {
            out += (index % 16 == 0) ? QLatin1Char('\n') : QLatin1Char(' ');
        }
        out += QStringLiteral("%1").arg(bytes[index], 2, 16, QLatin1Char('0')).toUpper();
    }
    if (bytes.size() > count) {
        out += QCoreApplication::translate(translation_context, "\n... %1 more bytes")
            .arg(static_cast<qulonglong>(bytes.size() - count));
    }
    return out;
}

EditorBuildResult finish_editor_build(
    std::expected<std::vector<uint8_t>, std::string> built,
    const char* translation_context,
    const char* failure_message,
    const char* success_message) {
    if (!built) {
        const auto error = utf8_to_qstring(built.error());
        return {
            .handled = true,
            .log_message = QCoreApplication::translate(translation_context, failure_message).arg(error),
            .warning_title = QCoreApplication::translate(translation_context, "Build failed"),
            .error = error,
        };
    }
    const auto size = built->size();
    return {
        .handled = true,
        .bytes = std::move(*built),
        .log_message = QCoreApplication::translate(translation_context, success_message)
            .arg(static_cast<qulonglong>(size)),
    };
}

std::expected<std::vector<uint8_t>, QString> read_file_bytes(const std::filesystem::path& path) {
    auto bytes = cricodecs::io::read_file_bytes(
        path,
        qstring_to_utf8(QCoreApplication::translate("Editor.EditorHelpers", "Editor file read failed")));
    if (!bytes) {
        return std::unexpected(utf8_to_qstring(bytes.error()));
    }
    return std::move(*bytes);
}

std::expected<void, QString> write_file_bytes(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return std::unexpected(QCoreApplication::translate("Editor.EditorHelpers", "could not create output folder: %1").arg(QString::fromStdString(ec.message())));
        }
    }

    QSaveFile file(path_to_qstring(path));
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(QCoreApplication::translate("Editor.EditorHelpers", "could not open output transaction: %1").arg(file.errorString()));
    }

    size_t written = 0;
    while (written < bytes.size()) {
        const auto remaining = bytes.size() - written;
        const auto chunk_size = static_cast<qint64>(std::min<size_t>(
            remaining,
            static_cast<size_t>(std::numeric_limits<qint64>::max())
        ));
        const auto count = file.write(
            reinterpret_cast<const char*>(bytes.data() + written),
            chunk_size
        );
        if (count <= 0) {
            file.cancelWriting();
            return std::unexpected(QCoreApplication::translate("Editor.EditorHelpers", "could not write output transaction: %1").arg(file.errorString()));
        }
        written += static_cast<size_t>(count);
    }

    if (!file.commit()) {
        return std::unexpected(QCoreApplication::translate("Editor.EditorHelpers", "could not commit output transaction: %1").arg(file.errorString()));
    }
    return {};
}

QString archive_kind_name(ArchiveKind kind) {
    switch (kind) {
    case ArchiveKind::Afs: return QCoreApplication::translate("Editor.EditorHelpers", "AFS archive");
    case ArchiveKind::Awb: return QCoreApplication::translate("Editor.EditorHelpers", "AWB/AFS2 archive");
    case ArchiveKind::Acx: return QCoreApplication::translate("Editor.EditorHelpers", "ACX archive");
    case ArchiveKind::Cpk: return QCoreApplication::translate("Editor.EditorHelpers", "CPK archive");
    case ArchiveKind::Cvm: return QCoreApplication::translate("Editor.EditorHelpers", "CVM/ROFS image");
    case ArchiveKind::None: break;
    }
    return QStringLiteral("Archive");
}

QString transform_kind_name(TransformKind kind) {
    switch (kind) {
    case TransformKind::AudioEncode: return QCoreApplication::translate("Editor.EditorHelpers", "Audio encode job");
    case TransformKind::MediaBuild: return QCoreApplication::translate("Editor.EditorHelpers", "USM/SFD build job");
    case TransformKind::Adx: return QCoreApplication::translate("Editor.EditorHelpers", "ADX/AHX stream");
    case TransformKind::Hca: return QCoreApplication::translate("Editor.EditorHelpers", "HCA stream");
    case TransformKind::Aax: return QCoreApplication::translate("Editor.EditorHelpers", "AAX segmented ADX");
    case TransformKind::Aix: return QCoreApplication::translate("Editor.EditorHelpers", "AIX layered ADX");
    case TransformKind::Usm: return QCoreApplication::translate("Editor.EditorHelpers", "USM/SofDec 2 inspector");
    case TransformKind::Sfd: return QCoreApplication::translate("Editor.EditorHelpers", "SFD/SofDec 1 inspector");
    case TransformKind::Csb: return QCoreApplication::translate("Editor.EditorHelpers", "CSB cue/archive inspector");
    case TransformKind::Acb: return QCoreApplication::translate("Editor.EditorHelpers", "ACB cue sheet inspector");
    case TransformKind::None: break;
    }
    return QStringLiteral("Transform");
}

QString cpk_preset_name(cricodecs::cpk::CpkPreset preset) {
    switch (preset) {
    case cricodecs::cpk::CpkPreset::Custom: return QStringLiteral("Custom");
    case cricodecs::cpk::CpkPreset::Id: return QStringLiteral("ID");
    case cricodecs::cpk::CpkPreset::Filename: return QStringLiteral("Filename");
    case cricodecs::cpk::CpkPreset::FilenameId: return QCoreApplication::translate("Editor.EditorHelpers", "Filename + ID");
    case cricodecs::cpk::CpkPreset::FilenameGroup: return QCoreApplication::translate("Editor.EditorHelpers", "Filename + Group");
    case cricodecs::cpk::CpkPreset::IdGroup: return QCoreApplication::translate("Editor.EditorHelpers", "ID + Group");
    case cricodecs::cpk::CpkPreset::FilenameIdGroup: return QCoreApplication::translate("Editor.EditorHelpers", "Filename + ID + Group");
    }
    return QStringLiteral("Unknown");
}

QString cpk_mode_name(cricodecs::cpk::CpkMode mode) {
    switch (mode) {
    case cricodecs::cpk::CpkMode::Mode0: return QCoreApplication::translate("Editor.EditorHelpers", "Mode 0 / ITOC");
    case cricodecs::cpk::CpkMode::Mode1: return QCoreApplication::translate("Editor.EditorHelpers", "Mode 1 / TOC");
    case cricodecs::cpk::CpkMode::Mode2: return QCoreApplication::translate("Editor.EditorHelpers", "Mode 2 / TOC + ITOC");
    case cricodecs::cpk::CpkMode::Mode3: return QCoreApplication::translate("Editor.EditorHelpers", "Mode 3 / TOC + ITOC + GTOC");
    }
    return QStringLiteral("Unknown");
}

std::expected<void, QString> extract_archive_bytes(
    ArchiveKind kind,
    std::vector<uint8_t> bytes,
    const std::filesystem::path& output_dir
) {
    switch (kind) {
    case ArchiveKind::Afs: {
        auto archive = cricodecs::afs::AfsContainer::load(std::span<const uint8_t>(bytes.data(), bytes.size()));
        if (!archive) {
            return std::unexpected(utf8_to_qstring(archive.error()));
        }
        if (auto result = archive->extract(output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    case ArchiveKind::Awb: {
        auto archive = cricodecs::awb::AwbContainer::load(std::move(bytes));
        if (!archive) {
            return std::unexpected(utf8_to_qstring(archive.error()));
        }
        if (auto result = archive->extract(output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    case ArchiveKind::Acx: {
        auto archive = cricodecs::acx::AcxContainer::load(std::span<const uint8_t>(bytes.data(), bytes.size()));
        if (!archive) {
            return std::unexpected(utf8_to_qstring(archive.error()));
        }
        if (auto result = archive->extract(output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    case ArchiveKind::Cpk: {
        auto archive = cricodecs::cpk::Cpk::load(std::move(bytes));
        if (!archive) {
            return std::unexpected(utf8_to_qstring(archive.error()));
        }
        if (auto result = archive->extract(output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    case ArchiveKind::Cvm: {
        if (auto result = modules::cvm::extract_all(std::span<const uint8_t>(bytes.data(), bytes.size()), output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    case ArchiveKind::None:
        break;
    }
    return std::unexpected(QCoreApplication::translate("Editor.EditorHelpers", "No archive object is available for extraction"));
}

std::expected<void, QString> transform_decode_to_wav(
    TransformKind kind,
    std::vector<uint8_t> bytes,
    const std::filesystem::path& output_path,
    DecryptionKeys keys
) {
    if (kind == TransformKind::Adx) {
        auto wav = modules::adx::decode_to_wav_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()), keys);
        if (!wav) {
            return std::unexpected(utf8_to_qstring(wav.error()));
        }
        return write_file_bytes(output_path, *wav);
    }

    if (kind == TransformKind::Hca) {
        auto wav = modules::hca::decode_to_wav_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()), keys);
        if (!wav) {
            return std::unexpected(utf8_to_qstring(wav.error()));
        }
        return write_file_bytes(output_path, *wav);
    }

    return std::unexpected(QCoreApplication::translate("Editor.EditorHelpers", "Decode to WAV is not available for this transform type"));
}

std::expected<void, QString> transform_write_bytes(
    TransformKind kind,
    std::vector<uint8_t> bytes,
    const std::filesystem::path& output_path,
    DecryptionKeys keys,
    QString action
) {
    if (kind == TransformKind::Adx) {
        const auto adx_action = action == QStringLiteral("decrypt")
            ? modules::adx::TransformAction::Decrypt
            : modules::adx::TransformAction::Rebuild;
        auto output = modules::adx::transform_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()), keys, adx_action);
        if (!output) {
            return std::unexpected(utf8_to_qstring(output.error()));
        }
        return write_file_bytes(output_path, *output);
    }

    if (kind == TransformKind::Hca) {
        modules::hca::TransformAction hca_action = modules::hca::TransformAction::Rebuild;
        if (action == QStringLiteral("decrypt")) {
            hca_action = modules::hca::TransformAction::Decrypt;
        } else if (action == QStringLiteral("encrypt")) {
            hca_action = modules::hca::TransformAction::Encrypt;
        }
        auto output = modules::hca::transform_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()), keys, hca_action);
        if (!output) {
            return std::unexpected(utf8_to_qstring(output.error()));
        }
        return write_file_bytes(output_path, *output);
    }

    if (kind == TransformKind::Aax) {
        auto aax = cricodecs::aax::AaxContainer::load(std::span<const uint8_t>(bytes.data(), bytes.size()));
        if (!aax) {
            return std::unexpected(utf8_to_qstring(aax.error()));
        }
        if (action == QStringLiteral("extract")) {
            auto adx = aax->adx_data();
            if (!adx) {
                return std::unexpected(utf8_to_qstring(adx.error()));
            }
            return write_file_bytes(output_path, *adx);
        }
        auto rebuilt = aax->save();
        if (!rebuilt) {
            return std::unexpected(utf8_to_qstring(rebuilt.error()));
        }
        return write_file_bytes(output_path, *rebuilt);
    }

    if (kind == TransformKind::Sfd) {
        auto sfd = cricodecs::sfd::SfdContainer::load(std::move(bytes));
        if (!sfd) {
            return std::unexpected(utf8_to_qstring(sfd.error()));
        }
        auto rebuilt = sfd->save();
        if (!rebuilt) {
            return std::unexpected(utf8_to_qstring(rebuilt.error()));
        }
        return write_file_bytes(output_path, *rebuilt);
    }

    if (kind == TransformKind::Csb) {
        auto csb = cricodecs::csb::CsbContainer::load(std::move(bytes));
        if (!csb) {
            return std::unexpected(utf8_to_qstring(csb.error()));
        }
        auto rebuilt = csb->save();
        if (!rebuilt) {
            return std::unexpected(utf8_to_qstring(rebuilt.error()));
        }
        return write_file_bytes(output_path, *rebuilt);
    }

    return write_file_bytes(output_path, bytes);
}

std::expected<void, QString> transform_extract_all(
    TransformKind kind,
    std::vector<uint8_t> bytes,
    const std::filesystem::path& output_dir
) {
    if (kind == TransformKind::Aax) {
        auto aax = cricodecs::aax::AaxContainer::load(std::span<const uint8_t>(bytes.data(), bytes.size()));
        if (!aax) {
            return std::unexpected(utf8_to_qstring(aax.error()));
        }
        std::error_code ec;
        std::filesystem::create_directories(output_dir, ec);
        if (ec) {
            return std::unexpected(QCoreApplication::translate("Editor.EditorHelpers", "could not create output folder: %1").arg(QString::fromStdString(ec.message())));
        }
        for (uint32_t index = 0; index < aax->segment_count(); ++index) {
            const auto output = output_dir / ("segment_" + std::to_string(index) + ".adx");
            if (auto result = aax->extract_file(index, output); !result) {
                return std::unexpected(utf8_to_qstring(result.error()));
            }
        }
        auto joined = aax->adx_data();
        if (!joined) {
            return std::unexpected(utf8_to_qstring(joined.error()));
        }
        return write_file_bytes(output_dir / "joined.adx", *joined);
    }
    if (kind == TransformKind::Aix) {
        if (auto result = modules::aix::extract_all(std::span<const uint8_t>(bytes.data(), bytes.size()), output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    if (kind == TransformKind::Usm) {
        cricodecs::usm::UsmReader usm;
        if (auto result = usm.load(std::span<const uint8_t>(bytes.data(), bytes.size())); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        if (auto result = usm.extract(output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    if (kind == TransformKind::Sfd) {
        auto sfd = cricodecs::sfd::SfdContainer::load(std::move(bytes));
        if (!sfd) {
            return std::unexpected(utf8_to_qstring(sfd.error()));
        }
        if (auto result = sfd->extract(output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    if (kind == TransformKind::Csb) {
        auto csb = cricodecs::csb::CsbContainer::load(std::move(bytes));
        if (!csb) {
            return std::unexpected(utf8_to_qstring(csb.error()));
        }
        if (auto result = csb->extract(output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    if (kind == TransformKind::Acb) {
        auto acb = cricodecs::acb::AcbContainer::load(std::move(bytes));
        if (!acb) {
            return std::unexpected(utf8_to_qstring(acb.error()));
        }
        if (auto result = acb->extract(output_dir); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
        return {};
    }
    return std::unexpected(QCoreApplication::translate("Editor.EditorHelpers", "Extract all is not available for this transform type"));
}

} // namespace cristudio
