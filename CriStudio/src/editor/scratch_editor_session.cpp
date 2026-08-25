#include "shared/i18n.hpp"
#include "editor/scratch_editor_session.hpp"

#include "editor/editor_helpers.hpp"
#include "modules/acx/acx_edit.hpp"
#include "modules/afs/afs_edit.hpp"
#include "modules/awb/awb_edit.hpp"
#include "modules/cpk/cpk_edit.hpp"
#include "modules/utf/utf_edit.hpp"
#include "path_text.hpp"

#include "cvm_builder.hpp"

#include <QCoreApplication>
#include <QFileInfo>

#include <span>

namespace cristudio {
namespace {

void push_log(ScratchEditorSession& session, QString message) {
    session.log_messages.push_back(std::move(message));
}

LoadedDocument failed_cvm_script_document(
    const QString& script_path_text,
    std::string format,
    std::vector<InfoRow> info,
    uintmax_t file_size = 0
) {
    return LoadedDocument{
        .display_name = qstring_to_utf8(QFileInfo(script_path_text).fileName()),
        .format = std::move(format),
        .file_size = file_size,
        .info = std::move(info)
    };
}

ScratchEditorSession build_job_session(
    TransformKind kind,
    std::string display_name,
    std::string format,
    std::vector<InfoRow> info,
    QString log_message
) {
    ScratchEditorSession session;
    session.transform_kind = kind;
    session.document = LoadedDocument{
        .display_name = std::move(display_name),
        .format = std::move(format),
        .info = std::move(info),
    };
    push_log(session, std::move(log_message));
    return session;
}

} // namespace

ScratchEditorSession create_scratch_editor_session(const EditorOpenRequest& request) {
    ScratchEditorSession session;

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::AudioEncode) {
        return build_job_session(
            TransformKind::AudioEncode,
            "EncodeAudio",
            cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Audio encode job"),
            {
                {"Source", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Scratch audio encode job")},
                {"Targets", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "ADX, AHX, HCA")},
                {"Input", "WAV"}
            },
            QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch audio encode job."));
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::MediaBuild) {
        return build_job_session(
            TransformKind::MediaBuild,
            "BuildMovie",
            cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "USM/SFD build job"),
            {
                {"Source", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Scratch USM/SFD build job")},
                {"Targets", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "USM, SFD")},
                {cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Video prep"), cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "prepared, FFmpeg VP9, H.264, or MPEG")},
                {cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Audio prep"), cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "no tracks, or ADX/HCA from prepared or FFmpeg-supported audio")}
            },
            QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch USM/SFD build job."));
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::AaxBuild) {
        return build_job_session(
            TransformKind::Aax,
            "BuildAax",
            cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "AAX ADX build job"),
            {
                {"Source", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Scratch AAX build job")},
                {"Input", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "one ADX/AHX file per segment")},
                {"Output", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "AAX UTF wrapper")},
                {"Loop", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "optional last-segment loop marker")}
            },
            QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch AAX ADX build job."));
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::AixBuild) {
        return build_job_session(
            TransformKind::Aix,
            "BuildAix",
            cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "AIX ADX build job"),
            {
                {"Source", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Scratch AIX build job")},
                {"Input", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "one line per segment, semicolon-separated ADX/AHX layers")},
                {"Output", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "AIX layered ADX container")}
            },
            QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch AIX ADX build job."));
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::CsbBuild) {
        return build_job_session(
            TransformKind::Csb,
            "BuildCsb",
            cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CSB folder build job"),
            {
                {"Source", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Scratch CSB folder build job")},
                {"Input", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "folder tree of CSB payload files")},
                {"Output", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CSB cue/archive")}
            },
            QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch CSB folder build job."));
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::Afs) {
        auto scratch = modules::afs::create_scratch_archive();
        session.afs = std::move(scratch.container);
        session.document = std::move(scratch.document);
        session.archive_kind = ArchiveKind::Afs;
        push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch AFS archive."));
        return session;
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::Awb) {
        auto scratch = modules::awb::create_scratch_archive();
        session.awb = std::move(scratch.container);
        session.document = std::move(scratch.document);
        session.archive_kind = ArchiveKind::Awb;
        push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch AWB/AFS2 archive."));
        return session;
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::Acx) {
        auto scratch = modules::acx::create_scratch_archive();
        session.acx = std::move(scratch.container);
        session.document = std::move(scratch.document);
        session.archive_kind = ArchiveKind::Acx;
        push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch ACX archive."));
        return session;
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::Cpk) {
        auto scratch = modules::cpk::create_scratch_archive(
            request.cpk_preset.value_or(cricodecs::cpk::CpkPreset::Filename));
        session.cpk = std::move(scratch.container);
        session.document = std::move(scratch.document);
        session.archive_kind = ArchiveKind::Cpk;
        push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch CPK archive."));
        return session;
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::CvmScript) {
        const auto script_path_text = path_to_qstring(request.source_path);
        auto script = cricodecs::cvm::CvmBuildScript::load(request.source_path);
        if (!script) {
            session.document = failed_cvm_script_document(
                script_path_text,
                cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CVM build script (failed)"),
                {
                    {"Source", qstring_to_utf8(script_path_text)},
                    {"Validation", script.error()}
                }
            );
            push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "CVM CVS load failed: %1").arg(utf8_to_qstring(script.error())));
            return session;
        }

        auto built = cricodecs::cvm::CvmBuilder{}.build(*script);
        if (!built) {
            session.document = failed_cvm_script_document(
                script_path_text,
                cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CVM build script (failed)"),
                {
                    {"Source", qstring_to_utf8(script_path_text)},
                    {"Disc", script->disc_name()},
                    {"Files", std::to_string(script->files().size())},
                    {"Validation", built.error()}
                }
            );
            push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "CVM CVS build failed: %1").arg(utf8_to_qstring(built.error())));
            return session;
        }

        session.bytes = std::move(*built);
        auto loaded = cricodecs::cvm::CvmContainer::load(
            std::span<const uint8_t>(session.bytes.data(), session.bytes.size()));
        if (!loaded) {
            session.document = LoadedDocument{
                .display_name = qstring_to_utf8(QFileInfo(script_path_text).completeBaseName() + QStringLiteral(".cvm")),
                .format = cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CVM build script (failed)"),
                .file_size = session.bytes.size(),
                .info = {
                    {"Source", qstring_to_utf8(script_path_text)},
                    {"Disc", script->disc_name()},
                    {"Files", std::to_string(script->files().size())},
                    {"Bytes", std::to_string(session.bytes.size())},
                    {"Validation", loaded.error()}
                }
            };
            push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "CVM CVS reload failed: %1").arg(utf8_to_qstring(loaded.error())));
            return session;
        }

        session.cvm = std::move(*loaded);
        session.archive_kind = ArchiveKind::Cvm;
        session.document = LoadedDocument{
            .display_name = qstring_to_utf8(QFileInfo(script_path_text).completeBaseName() + QStringLiteral(".cvm")),
            .format = cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CVM/ROFS image (from CVS)"),
            .file_size = session.bytes.size(),
            .info = {
                {"Source", qstring_to_utf8(script_path_text)},
                {"Disc", script->disc_name()},
                {"Media", script->media()},
                {"Files", std::to_string(script->files().size())},
                {"Bytes", std::to_string(session.bytes.size())}
            },
            .entry_columns = {"Index", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Archive Path"), cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Extent Sector"), "Size"},
            .entry_column_types = {"integer", "path", "integer", "size"},
            .entries = {}
        };
        session.title = session.document->display_name;
        push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "Built scratch CVM session from %1.").arg(script_path_text));
        return session;
    }

    if (request.scratch_kind == EditorOpenRequest::ScratchKind::CvmDirectory) {
        const auto input_dir_text = path_to_qstring(request.source_path);
        const auto options = request.cvm_directory_options.value_or(cricodecs::cvm::CvmBuildDirectoryOptions{});
        auto input = cricodecs::cvm::CvmBuildInput::from_directory(request.source_path, options);
        if (!input) {
            session.document = LoadedDocument{
                .display_name = qstring_to_utf8(QFileInfo(input_dir_text).fileName() + QStringLiteral(".cvm")),
                .format = cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CVM directory build (failed)"),
                .file_size = 0,
                .info = {
                    {"Source", qstring_to_utf8(input_dir_text)},
                    {"Disc", options.disc_name},
                    {"Media", options.media},
                    {"Validation", input.error()}
                }
            };
            push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "CVM directory scan failed: %1").arg(utf8_to_qstring(input.error())));
            return session;
        }

        auto built = cricodecs::cvm::CvmBuilder{}.build(*input);
        if (!built) {
            session.document = LoadedDocument{
                .display_name = qstring_to_utf8(QFileInfo(input_dir_text).fileName() + QStringLiteral(".cvm")),
                .format = cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CVM directory build (failed)"),
                .file_size = 0,
                .info = {
                    {"Source", qstring_to_utf8(input_dir_text)},
                    {"Disc", input->disc_name},
                    {"Media", input->media},
                    {"Files", std::to_string(input->files.size())},
                    {"Validation", built.error()}
                }
            };
            push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "CVM directory build failed: %1").arg(utf8_to_qstring(built.error())));
            return session;
        }

        session.bytes = std::move(*built);
        auto loaded = cricodecs::cvm::CvmContainer::load(
            std::span<const uint8_t>(session.bytes.data(), session.bytes.size()));
        if (!loaded) {
            session.document = LoadedDocument{
                .display_name = input->disc_name,
                .format = cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CVM directory build (failed)"),
                .file_size = session.bytes.size(),
                .info = {
                    {"Source", qstring_to_utf8(input_dir_text)},
                    {"Disc", input->disc_name},
                    {"Media", input->media},
                    {"Files", std::to_string(input->files.size())},
                    {"Bytes", std::to_string(session.bytes.size())},
                    {"Validation", loaded.error()}
                }
            };
            push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "CVM directory reload failed: %1").arg(utf8_to_qstring(loaded.error())));
            return session;
        }

        session.cvm = std::move(*loaded);
        session.archive_kind = ArchiveKind::Cvm;
        session.document = LoadedDocument{
            .display_name = input->disc_name,
            .format = cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "CVM/ROFS image (from directory)"),
            .file_size = session.bytes.size(),
            .info = {
                {"Source", qstring_to_utf8(input_dir_text)},
                {"Disc", input->disc_name},
                {"Media", input->media},
                {"Files", std::to_string(input->files.size())},
                {"Bytes", std::to_string(session.bytes.size())}
            },
            .entry_columns = {"Index", cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Archive Path"), cristudio::i18n::translate_utf8("Editor.ScratchEditorSession", "Extent Sector"), "Size"},
            .entry_column_types = {"integer", "path", "integer", "size"},
            .entries = {}
        };
        session.title = session.document->display_name;
        push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "Built scratch CVM session from directory %1.").arg(input_dir_text));
        return session;
    }

    auto scratch = modules::utf::create_scratch_table_session();
    session.utf = std::move(scratch.table);
    session.bytes = std::move(scratch.bytes);
    session.document = std::move(scratch.document);
    push_log(session, QCoreApplication::translate("Editor.ScratchEditorSession", "Created scratch UTF table."));
    return session;
}

} // namespace cristudio
