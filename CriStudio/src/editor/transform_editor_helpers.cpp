#include "shared/i18n.hpp"
#include <QCoreApplication>
#include "editor/transform_editor_helpers.hpp"

#include "modules/aax/aax_edit.hpp"
#include "modules/acb/acb_edit.hpp"
#include "modules/adx/adx_edit.hpp"
#include "modules/adx/adx_edit_ui.hpp"
#include "modules/aix/aix_edit.hpp"
#include "modules/audio/audio_encode.hpp"
#include "modules/csb/csb_edit.hpp"
#include "modules/hca/hca_edit.hpp"
#include "modules/hca/hca_edit_ui.hpp"
#include "modules/sfd/sfd_edit.hpp"
#include "modules/usm/media_build.hpp"
#include "modules/usm/usm_edit.hpp"
#include "path_text.hpp"

#include <string>
#include <utility>

namespace cristudio {
namespace {

void append_detail_info_rows(
    std::vector<InfoRow>& rows,
    const std::vector<modules::TransformDetailRow>& details,
    size_t max_rows
) {
    size_t added = 0;
    size_t omitted = 0;
    for (const auto& detail : details) {
        if (detail.payload_kind != 0) {
            continue;
        }
        if (added >= max_rows) {
            ++omitted;
            continue;
        }
        rows.push_back({qstring_to_utf8(detail.field), qstring_to_utf8(detail.value)});
        ++added;
    }
    (void)omitted;
}

} // namespace

std::expected<std::vector<uint8_t>, QString> transform_payload_preview_bytes(
    const TransformSessionView& view,
    TransformPayloadSelection selection
) {
    if (selection.payload_kind == 1 && view.aax != nullptr) {
        if (selection.index < 0) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "AAX segment preview failed: index out of range"));
        }
        auto data = view.aax->segment_data(static_cast<uint32_t>(selection.index));
        if (!data) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "AAX segment preview failed: %1").arg(utf8_to_qstring(data.error())));
        }
        return std::vector<uint8_t>(data->begin(), data->end());
    }

    if (selection.payload_kind == 2 && view.aix != nullptr) {
        if (selection.index < 0 || selection.layer < 0) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "AIX payload preview failed: index out of range"));
        }
        auto data = view.aix->segment_bytes(static_cast<size_t>(selection.index), static_cast<size_t>(selection.layer));
        if (!data) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "AIX payload preview failed: %1").arg(utf8_to_qstring(data.error())));
        }
        return std::move(*data);
    }

    if (selection.payload_kind == 4 && view.sfd != nullptr) {
        if (selection.index < 0) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "SFD stream preview failed: index out of range"));
        }
        auto data = view.sfd->extract_stream(static_cast<uint32_t>(selection.index));
        if (!data) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "SFD stream preview failed: %1").arg(utf8_to_qstring(data.error())));
        }
        return std::move(*data);
    }

    if (selection.payload_kind == 5 && view.csb != nullptr) {
        if (selection.index < 0) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "CSB stream preview failed: index out of range"));
        }
        auto data = view.csb->stream_data(static_cast<uint32_t>(selection.index));
        if (!data) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "CSB stream preview failed: %1").arg(utf8_to_qstring(data.error())));
        }
        return std::move(*data);
    }

    if (selection.payload_kind == 6 && view.acb != nullptr && view.keys != nullptr) {
        if (selection.index < 0) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "ACB waveform preview failed: index out of range"));
        }
        auto data = view.acb->extract_waveform_data(
            static_cast<uint32_t>(selection.index),
            view.keys->has_cri_key ? view.keys->cri_key : 0
        );
        if (!data) {
            return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "ACB waveform preview failed: %1").arg(utf8_to_qstring(data.error())));
        }
        return std::move(*data);
    }

    return std::unexpected(QCoreApplication::translate("Editor.TransformEditorHelpers", "No binary payload preview"));
}

QString transform_payload_preview_text(
    const TransformSessionView& view,
    TransformPayloadSelection selection,
    std::span<const uint8_t> fallback_bytes
) {
    const auto payload_kind = selection.payload_kind;
    const auto index = selection.index;

    if (payload_kind == 1 && view.aax != nullptr) {
        auto preview = modules::aax::segment_payload_preview(*view.aax, index);
        return preview ? *preview : preview.error();
    }

    if (payload_kind == 2 && view.aix != nullptr) {
        auto preview = modules::aix::segment_payload_preview(*view.aix, index, selection.layer);
        return preview ? *preview : preview.error();
    }

    if (payload_kind == 3 && view.usm != nullptr) {
        auto preview = modules::usm::chunk_payload_preview(*view.usm, index);
        return preview ? *preview : preview.error();
    }

    if (payload_kind == 10 && view.usm != nullptr) {
        auto preview = modules::usm::utf_payload_preview(*view.usm, index);
        return preview ? *preview : preview.error();
    }

    if (payload_kind == 13 && view.usm != nullptr) {
        auto preview = modules::usm::stream_payload_preview(*view.usm, index);
        return preview ? *preview : preview.error();
    }

    if ((payload_kind == 4 || payload_kind == 11 || payload_kind == 12) && view.sfd != nullptr) {
        auto preview = modules::sfd::payload_preview(*view.sfd, payload_kind, index);
        return preview ? *preview : preview.error();
    }

    if ((payload_kind == 5 || payload_kind == 8 || payload_kind == 9) && view.csb != nullptr) {
        auto preview = modules::csb::payload_preview(*view.csb, payload_kind, index);
        return preview ? *preview : preview.error();
    }

    if ((payload_kind == 6 || payload_kind == 7) && view.acb != nullptr && view.keys != nullptr) {
        auto preview = modules::acb::payload_preview(*view.acb, *view.keys, payload_kind, index);
        return preview ? *preview : preview.error();
    }

    return hex_preview(fallback_bytes);
}

std::vector<modules::TransformDetailRow> transform_detail_rows(
    TransformKind kind,
    const TransformSessionView& view
) {
    switch (kind) {
    case TransformKind::AudioEncode:
        return view.keys != nullptr ? modules::audio::encode_job_detail_rows(*view.keys) : std::vector<modules::TransformDetailRow>{};
    case TransformKind::MediaBuild:
        return view.keys != nullptr ? modules::usm::media_build_job_detail_rows(*view.keys) : std::vector<modules::TransformDetailRow>{};
    case TransformKind::Adx:
        return view.adx != nullptr ? modules::adx::detail_rows(*view.adx) : std::vector<modules::TransformDetailRow>{};
    case TransformKind::Hca:
        return view.hca != nullptr ? modules::hca::detail_rows(*view.hca) : std::vector<modules::TransformDetailRow>{};
    case TransformKind::Aax:
        return view.aax != nullptr ? modules::aax::detail_rows(*view.aax) : modules::aax::build_job_detail_rows();
    case TransformKind::Aix:
        return view.aix != nullptr ? modules::aix::detail_rows(*view.aix) : modules::aix::build_job_detail_rows();
    case TransformKind::Usm:
        return view.usm != nullptr ? modules::usm::detail_rows(*view.usm) : std::vector<modules::TransformDetailRow>{};
    case TransformKind::Sfd:
        return view.sfd != nullptr ? modules::sfd::detail_rows(*view.sfd) : std::vector<modules::TransformDetailRow>{};
    case TransformKind::Csb:
        return view.csb != nullptr ? modules::csb::detail_rows(*view.csb) : modules::csb::build_job_detail_rows();
    case TransformKind::Acb:
        return view.acb != nullptr && view.keys != nullptr
            ? modules::acb::detail_rows(*view.acb, *view.keys)
            : std::vector<modules::TransformDetailRow>{};
    case TransformKind::None:
        break;
    }
    return {};
}

void append_transform_info_rows(
    std::vector<InfoRow>& rows,
    TransformKind kind,
    const TransformSessionView& view,
    size_t max_rows
) {
    if (rows.empty()) {
        return;
    }

    switch (kind) {
    case TransformKind::AudioEncode:
        rows.back().value = cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "WAV source encode job");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Transform kind"), cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Audio encode")});
        break;
    case TransformKind::MediaBuild:
        rows.back().value = cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "USM/SFD source build job");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Transform kind"), cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "USM/SFD build")});
        break;
    case TransformKind::Adx:
        if (view.adx == nullptr) return;
        rows.back().value = cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "ADX/AHX native decode, decrypt, and rebuild path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Transform kind"), cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "ADX/AHX")});
        break;
    case TransformKind::Hca:
        if (view.hca == nullptr) return;
        rows.back().value = cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "HCA native decode, encrypt, decrypt, and rebuild path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Transform kind"), "HCA"});
        break;
    case TransformKind::Aax:
        rows.back().value = view.aax != nullptr
            ? cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "AAX native save, segment export, and joined ADX export path available")
            : cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "AAX source build job");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Transform kind"), view.aax != nullptr ? "AAX" : cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "AAX build")});
        break;
    case TransformKind::Aix:
        rows.back().value = view.aix != nullptr
            ? cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "AIX native layered ADX inspection and extraction path available")
            : cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "AIX source build job");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Transform kind"), view.aix != nullptr ? "AIX" : cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "AIX build")});
        break;
    case TransformKind::Usm:
        if (view.usm == nullptr) return;
        rows.back().value = cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "USM native chunk/stream inspection, demux, and builder-input path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Inspector kind"), cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "USM/SofDec 2")});
        break;
    case TransformKind::Sfd:
        if (view.sfd == nullptr) return;
        rows.back().value = cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "SFD native stream inspection, extraction, save, and builder-input path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Inspector kind"), cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "SFD/SofDec 1")});
        break;
    case TransformKind::Csb:
        rows.back().value = view.csb != nullptr
            ? cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "CSB native section/stream inspection, extraction, and rebuild path available")
            : cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "CSB source build job");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Inspector kind"), view.csb != nullptr ? "CSB" : cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "CSB folder build")});
        break;
    case TransformKind::Acb:
        if (view.acb == nullptr) return;
        rows.back().value = cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "ACB native cue/waveform inspection and extraction path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.TransformEditorHelpers", "Inspector kind"), "ACB"});
        break;
    case TransformKind::None:
        return;
    }

    append_detail_info_rows(rows, transform_detail_rows(kind, view), max_rows);
}

EditorBuildResult build_transform_session_bytes(
    TransformKind kind,
    const TransformSessionView& view
) {
    switch (kind) {
    case TransformKind::Adx:
        if (view.adx != nullptr) {
            return finish_editor_build(
                modules::adx::build_session_bytes(*view.adx),
                "Editor.TransformEditorHelpers",
                "ADX/AHX save failed: %1",
                "Rebuilt ADX/AHX session bytes: %1 bytes");
        }
        break;
    case TransformKind::Hca:
        if (view.hca != nullptr) {
            return finish_editor_build(
                modules::hca::build_session_bytes(*view.hca),
                "Editor.TransformEditorHelpers",
                "HCA save failed: %1",
                "Rebuilt HCA session bytes: %1 bytes");
        }
        break;
    case TransformKind::Aax:
        if (view.aax != nullptr) {
            return finish_editor_build(
                modules::aax::build_session_bytes(*view.aax),
                "Editor.TransformEditorHelpers",
                "AAX save failed: %1",
                "Rebuilt AAX session bytes: %1 bytes");
        }
        break;
    case TransformKind::Aix:
        if (view.aix != nullptr) {
            auto built = modules::aix::rebuild_session_bytes(*view.aix);
            if (!built) {
            return {
                .handled = true,
                .log_message = QCoreApplication::translate("Editor.TransformEditorHelpers", "AIX rebuild failed: %1").arg(utf8_to_qstring(built.error())),
                .warning_title = QCoreApplication::translate("Editor.TransformEditorHelpers", "Build failed"),
                .error = utf8_to_qstring(built.error())
            };
            }
            const auto byte_count = built->size();
            cricodecs::aix::Aix reloaded;
            QString reload_log;
            if (auto result = reloaded.load(std::span<const uint8_t>(built->data(), built->size())); result) {
                *view.aix = std::move(reloaded);
            } else {
                reload_log = QCoreApplication::translate("Editor.TransformEditorHelpers", "AIX rebuild reload failed: %1").arg(utf8_to_qstring(result.error()));
            }
            return {
                .handled = true,
                .bytes = std::move(*built),
                .log_message = reload_log.isEmpty()
                    ? QCoreApplication::translate("Editor.TransformEditorHelpers", "Rebuilt AIX session bytes: %1 bytes").arg(static_cast<qulonglong>(byte_count))
                    : reload_log + QStringLiteral("\n") +
                        QCoreApplication::translate("Editor.TransformEditorHelpers", "Rebuilt AIX session bytes: %1 bytes").arg(static_cast<qulonglong>(byte_count))
            };
        }
        break;
    case TransformKind::Usm:
        if (view.usm != nullptr) {
            return {
                .handled = true,
                .log_message = QCoreApplication::translate("Editor.TransformEditorHelpers", "USM loaded-object rebuild is not exposed by the native API; use Extract or a source-backed build wizard.")
            };
        }
        break;
    case TransformKind::Sfd:
        if (view.sfd != nullptr) {
            return finish_editor_build(
                modules::sfd::build_session_bytes(*view.sfd),
                "Editor.TransformEditorHelpers",
                "SFD save failed: %1",
                "Rebuilt SFD session bytes: %1 bytes");
        }
        break;
    case TransformKind::Csb:
        if (view.csb != nullptr) {
            return finish_editor_build(
                modules::csb::build_session_bytes(*view.csb),
                "Editor.TransformEditorHelpers",
                "CSB save failed: %1",
                "Rebuilt CSB session bytes: %1 bytes");
        }
        break;
    case TransformKind::Acb:
        if (view.acb != nullptr) {
            return {
                .handled = true,
                .log_message = QCoreApplication::translate("Editor.TransformEditorHelpers", "ACB mutable authoring is not exposed by the native API; keeping source bytes.")
            };
        }
        break;
    case TransformKind::AudioEncode:
    case TransformKind::MediaBuild:
    case TransformKind::None:
        break;
    }
    return {};
}

EditorBuildResult edit_transform_options(
    QWidget* parent,
    TransformKind kind,
    const TransformSessionView& view
) {
    if (view.keys == nullptr) {
        return {};
    }

    if (kind == TransformKind::Adx && view.adx != nullptr) {
        auto config = modules::adx::choose_rebuild_config(parent, *view.adx, *view.keys);
        if (!config) {
            return {
                .handled = true,
                .warning_title = view.adx->is_ahx()
                    ? QCoreApplication::translate("Editor.TransformEditorHelpers", "AHX rebuild failed")
                    : QCoreApplication::translate("Editor.TransformEditorHelpers", "ADX rebuild failed"),
                .error = config.error()
            };
        }
        if (!*config) {
            return {.handled = true};
        }

        auto built = modules::adx::rebuild_session_bytes(*view.adx, *view.keys, **config);
        if (!built) {
            return {
                .handled = true,
                .log_message = QCoreApplication::translate("Editor.TransformEditorHelpers", "ADX rebuild options failed: %1").arg(utf8_to_qstring(built.error())),
                .warning_title = view.adx->is_ahx()
                    ? QCoreApplication::translate("Editor.TransformEditorHelpers", "AHX rebuild failed")
                    : QCoreApplication::translate("Editor.TransformEditorHelpers", "ADX rebuild failed"),
                .error = utf8_to_qstring(built.error())
            };
        }

        auto reloaded = cricodecs::adx::Adx::load(std::span<const uint8_t>(built->data(), built->size()));
        if (!reloaded) {
            return {
                .handled = true,
                .log_message = QCoreApplication::translate("Editor.TransformEditorHelpers", "ADX rebuilt bytes failed reload: %1").arg(utf8_to_qstring(reloaded.error())),
                .warning_title = QCoreApplication::translate("Editor.TransformEditorHelpers", "ADX reload failed"),
                .error = utf8_to_qstring(reloaded.error())
            };
        }

        *view.adx = std::move(*reloaded);
        return {
            .handled = true,
            .bytes = std::move(*built),
            .log_message = QCoreApplication::translate("Editor.TransformEditorHelpers", "Rebuilt ADX/AHX session with updated options.")
        };
    }

    if (kind == TransformKind::Hca && view.hca != nullptr) {
        auto config = modules::hca::choose_rebuild_config(parent, *view.hca, *view.keys);
        if (!config) {
            return {
                .handled = true,
                .warning_title = QCoreApplication::translate("Editor.TransformEditorHelpers", "HCA rebuild failed"),
                .error = config.error()
            };
        }
        if (!*config) {
            return {.handled = true};
        }

        auto built = modules::hca::rebuild_session_bytes(*view.hca, *view.keys, **config);
        if (!built) {
            return {
                .handled = true,
                .log_message = QCoreApplication::translate("Editor.TransformEditorHelpers", "HCA rebuild options failed: %1").arg(utf8_to_qstring(built.error())),
                .warning_title = QCoreApplication::translate("Editor.TransformEditorHelpers", "HCA rebuild failed"),
                .error = utf8_to_qstring(built.error())
            };
        }

        auto reloaded = cricodecs::hca::Hca::load(std::span<const uint8_t>(built->data(), built->size()));
        if (!reloaded) {
            return {
                .handled = true,
                .log_message = QCoreApplication::translate("Editor.TransformEditorHelpers", "HCA rebuilt bytes failed reload: %1").arg(utf8_to_qstring(reloaded.error())),
                .warning_title = QCoreApplication::translate("Editor.TransformEditorHelpers", "HCA reload failed"),
                .error = utf8_to_qstring(reloaded.error())
            };
        }

        *view.hca = std::move(*reloaded);
        return {
            .handled = true,
            .bytes = std::move(*built),
            .log_message = QCoreApplication::translate("Editor.TransformEditorHelpers", "Rebuilt HCA session with updated options.")
        };
    }

    return {};
}

} // namespace cristudio
