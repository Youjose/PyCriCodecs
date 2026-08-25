#include "modules/usm/media_build_ui.hpp"

#include "editor/editor_helpers.hpp"
#include "editor/editor_widgets.hpp"
#include "main_window/preview_helpers.hpp"
#include "path_text.hpp"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

namespace cristudio::modules::usm {
namespace {

QString audio_source_filter() {
    return QCoreApplication::translate(
        "Usm.MediaBuildUi",
        "Audio files (*.adx *.hca *.wav *.flac *.mp3 *.ogg *.opus *.m4a *.aac *.wma *.aif *.aiff);;All files (*)"
    );
}

QString key_display(uint64_t key) {
    return QStringLiteral("%1").arg(key, 16, 16, QLatin1Char('0')).toUpper();
}

std::optional<QString> apply_cri_key_text(DecryptionKeys& keys, QString text, int base) {
    text = text.trimmed();
    if (text.isEmpty()) {
        keys.has_cri_key = false;
        keys.cri_key = 0;
        return std::nullopt;
    }
    text.remove(QLatin1Char('_'));
    if (base == 16) {
        text.remove(QLatin1Char(' '));
        if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            text.remove(0, 2);
        }
    }
    bool ok = false;
    const auto value = text.toULongLong(&ok, base);
    if (!ok) {
        keys.has_cri_key = false;
        keys.cri_key = 0;
        return QCoreApplication::translate("Usm.MediaBuildUi", "CRI key is not valid %1.").arg(base == 16 ? QStringLiteral("hexadecimal") : QStringLiteral("decimal"));
    }
    keys.has_cri_key = true;
    keys.cri_key = value;
    return std::nullopt;
}

QWidget* replacement_picker_row(QDialog& dialog, QLineEdit& edit, const QString& title, const QString& filter) {
    edit.setReadOnly(true);
    edit.setClearButtonEnabled(false);
    edit.setPlaceholderText(QCoreApplication::translate("Usm.MediaBuildUi", "Keep current stream"));
    auto* row = new QWidget(&dialog);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(&edit, 1);
    auto* choose = new QPushButton(QCoreApplication::translate("Usm.MediaBuildUi", "Choose..."), row);
    auto* clear = new QToolButton(row);
    clear->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Clear"));
    clear->setToolTip(QCoreApplication::translate("Usm.MediaBuildUi", "Keep the current stream"));
    layout->addWidget(choose, 0);
    layout->addWidget(clear, 0);
    QObject::connect(choose, &QPushButton::clicked, &dialog, [&dialog, &edit, title, filter] {
        const auto selected = QFileDialog::getOpenFileName(&dialog, title, edit.text(), filter);
        if (!selected.isEmpty()) {
            edit.setText(selected);
        }
    });
    QObject::connect(clear, &QToolButton::clicked, &edit, &QLineEdit::clear);
    return row;
}

QString fourcc_text(cricodecs::usm::UsmChunkType value) {
    const auto raw = static_cast<uint32_t>(value);
    char chars[5] = {
        static_cast<char>((raw >> 24) & 0xFF),
        static_cast<char>((raw >> 16) & 0xFF),
        static_cast<char>((raw >> 8) & 0xFF),
        static_cast<char>(raw & 0xFF),
        '\0'
    };
    return QString::fromLatin1(chars, 4);
}

MediaBuildConfig::ExistingUsmTrack::Kind stream_kind(cricodecs::usm::UsmChunkType stream_id) {
    if (stream_id == cricodecs::usm::UsmChunkType::SFA) {
        return MediaBuildConfig::ExistingUsmTrack::Kind::Audio;
    }
    if (stream_id == cricodecs::usm::UsmChunkType::SBT) {
        return MediaBuildConfig::ExistingUsmTrack::Kind::Subtitle;
    }
    if (stream_id == cricodecs::usm::UsmChunkType::SFV) {
        return MediaBuildConfig::ExistingUsmTrack::Kind::Video;
    }
    if (stream_id == cricodecs::usm::UsmChunkType::ALP) {
        return MediaBuildConfig::ExistingUsmTrack::Kind::Alpha;
    }
    return MediaBuildConfig::ExistingUsmTrack::Kind::Unsupported;
}

QString track_kind_text(MediaBuildConfig::ExistingUsmTrack::Kind kind) {
    switch (kind) {
    case MediaBuildConfig::ExistingUsmTrack::Kind::Video:
        return QCoreApplication::translate("Usm.MediaBuildUi", "Video");
    case MediaBuildConfig::ExistingUsmTrack::Kind::Alpha:
        return QCoreApplication::translate("Usm.MediaBuildUi", "Alpha video");
    case MediaBuildConfig::ExistingUsmTrack::Kind::Audio:
        return QStringLiteral("Audio");
    case MediaBuildConfig::ExistingUsmTrack::Kind::Subtitle:
        return QStringLiteral("Subtitle");
    case MediaBuildConfig::ExistingUsmTrack::Kind::Unsupported:
        return QStringLiteral("Unsupported");
    }
    return QStringLiteral("Stream");
}

QString stream_label(const cricodecs::usm::UsmStreamInfo& stream, uint32_t index) {
    const auto filename = utf8_to_qstring(stream.filename.empty() ? stream.filename_raw : stream.filename);
    const auto codec = stream.audio_codec.has_value()
        ? QStringLiteral(" %1").arg(QString::fromLatin1(cricodecs::usm::audio_codec_name(*stream.audio_codec)))
        : QString{};
    return QCoreApplication::translate("Usm.MediaBuildUi", "%1 %2 ch %3%4%5")
        .arg(index)
        .arg(fourcc_text(stream.stream_id))
        .arg(stream.channel_no)
        .arg(codec)
        .arg(filename.isEmpty() ? QString{} : QStringLiteral("  %1").arg(filename));
}

QComboBox* make_video_prep_combo(QWidget* parent, MediaVideoPrep default_value) {
    auto* combo = new QComboBox(parent);
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "Use prepared stream"), static_cast<int>(MediaVideoPrep::UsePrepared));
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "FFmpeg VP9 / IVF"), static_cast<int>(MediaVideoPrep::FfmpegVp9));
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "FFmpeg H.264"), static_cast<int>(MediaVideoPrep::FfmpegH264));
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "FFmpeg MPEG-1"), static_cast<int>(MediaVideoPrep::FfmpegMpeg1));
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "FFmpeg MPEG-2"), static_cast<int>(MediaVideoPrep::FfmpegMpeg2));
    combo->setCurrentIndex((std::max)(0, combo->findData(static_cast<int>(default_value))));
    return combo;
}

QComboBox* make_audio_prep_combo(QWidget* parent, MediaAudioPrep default_value) {
    auto* combo = new QComboBox(parent);
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "Use prepared stream"), static_cast<int>(MediaAudioPrep::UsePrepared));
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "Encode to ADX (native WAV / FFmpeg fallback)"), static_cast<int>(MediaAudioPrep::FfmpegToAdx));
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "Encode to HCA (native WAV / FFmpeg fallback)"), static_cast<int>(MediaAudioPrep::FfmpegToHca));
    combo->setCurrentIndex((std::max)(0, combo->findData(static_cast<int>(default_value))));
    return combo;
}

QComboBox* subtitle_format_combo(QWidget* parent, cricodecs::usm::UsmSubtitleFormat default_value) {
    auto* combo = new QComboBox(parent);
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "Auto"), static_cast<int>(cricodecs::usm::UsmSubtitleFormat::Auto));
    combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "CRI source text"), static_cast<int>(cricodecs::usm::UsmSubtitleFormat::SourceText));
    combo->addItem(QStringLiteral("SRT"), static_cast<int>(cricodecs::usm::UsmSubtitleFormat::Srt));
    combo->addItem(QStringLiteral("ASS/SSA"), static_cast<int>(cricodecs::usm::UsmSubtitleFormat::Ass));
    combo->addItem(QStringLiteral("SBT"), static_cast<int>(cricodecs::usm::UsmSubtitleFormat::Sbt));
    combo->setCurrentIndex((std::max)(0, combo->findData(static_cast<int>(default_value))));
    return combo;
}

struct ExistingTrackControls {
    MediaBuildConfig::ExistingUsmTrack base;
    QCheckBox* enabled = nullptr;
    QLineEdit* filename = nullptr;
    QLineEdit* replacement = nullptr;
    QComboBox* prep = nullptr;
    QSpinBox* language = nullptr;
};

struct AudioTrackControls {
    QWidget* row = nullptr;
    QLineEdit* source = nullptr;
    QLineEdit* filename = nullptr;
    QSpinBox* channel = nullptr;
};

struct SubtitleTrackControls {
    QWidget* row = nullptr;
    QLineEdit* source = nullptr;
    QLineEdit* filename = nullptr;
    QComboBox* format = nullptr;
    QSpinBox* language = nullptr;
    QSpinBox* channel = nullptr;
};

std::optional<uint8_t> optional_channel(const QSpinBox& spin) {
    const int value = spin.value();
    return value < 0
        ? std::nullopt
        : std::optional<uint8_t>{static_cast<uint8_t>(value)};
}

QSpinBox* channel_spin(QWidget* parent) {
    auto* spin = new QSpinBox(parent);
    spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    spin->setRange(-1, (std::numeric_limits<uint8_t>::max)());
    spin->setSpecialValueText(QCoreApplication::translate("Usm.MediaBuildUi", "Auto"));
    spin->setValue(-1);
    spin->setMinimumWidth(112);
    spin->setToolTip(QCoreApplication::translate("Usm.MediaBuildUi", "Auto selects the lowest unused channel."));
    return spin;
}

MediaAudioPrep audio_prep_for_source(const std::filesystem::path& source, MediaAudioPrep target) {
    auto extension = path_to_qstring(source.extension()).toLower();
    if ((target == MediaAudioPrep::FfmpegToAdx && extension == QStringLiteral(".adx")) ||
        (target == MediaAudioPrep::FfmpegToHca && extension == QStringLiteral(".hca"))) {
        return MediaAudioPrep::UsePrepared;
    }
    return target;
}

} // namespace

std::expected<std::optional<MediaBuildConfig>, QString> choose_media_build_config(
    QWidget* parent,
    QString title,
    DecryptionKeys keys,
    bool prefer_sfd,
    const cricodecs::usm::UsmReader* current_usm,
    const std::filesystem::path& current_usm_path,
    std::span<const uint8_t> current_usm_bytes
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Usm.MediaBuildUi", "USM/SFD Build Wizard"));
    dialog.setMinimumWidth(660);
    dialog.setMinimumHeight(520);
    dialog.resize(760, 720);
    auto* layout = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 6, 0);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    content_layout->addLayout(form);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    auto* target_combo = new QComboBox(&dialog);
    target_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "USM / SofDec 2"), static_cast<int>(MediaBuildTarget::Usm));
    target_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "SFD / SofDec 1"), static_cast<int>(MediaBuildTarget::Sfd));
    target_combo->setCurrentIndex(prefer_sfd ? 1 : 0);
    if (current_usm != nullptr) {
        target_combo->setCurrentIndex(0);
        target_combo->setEnabled(false);
    }
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Target"), target_combo);

    auto* cri_key_edit = new QLineEdit(&dialog);
    cri_key_edit->setPlaceholderText(QCoreApplication::translate("Usm.MediaBuildUi", "Optional 64-bit CRI key"));
    if (keys.has_cri_key) {
        cri_key_edit->setText(key_display(keys.cri_key));
    }
    auto* cri_key_base = new QComboBox(&dialog);
    cri_key_base->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "hex"), 16);
    cri_key_base->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "dec"), 10);
    auto* cri_key_row = new QWidget(&dialog);
    auto* cri_key_layout = new QHBoxLayout(cri_key_row);
    cri_key_layout->setContentsMargins(0, 0, 0, 0);
    cri_key_layout->setSpacing(6);
    cri_key_layout->addWidget(cri_key_edit, 1);
    cri_key_layout->addWidget(cri_key_base, 0);
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "CRI key"), cri_key_row);

    auto* encrypt_audio_check = new QCheckBox(QCoreApplication::translate("Usm.MediaBuildUi", "Encrypt all audio tracks"), &dialog);
    encrypt_audio_check->setToolTip(QCoreApplication::translate(
        "Usm.MediaBuildUi",
        "USM audio encryption is container-wide: every ADX track is masked and every plain HCA track uses cipher type 56."
    ));
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Audio encryption"), encrypt_audio_check);

    auto* video_edit = new QLineEdit(&dialog);
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Video"), path_picker_row(
        dialog, *video_edit, QCoreApplication::translate("Usm.MediaBuildUi", "Browse"),
        QCoreApplication::translate("Usm.MediaBuildUi", "Choose video source"), PathPickerMode::OpenFile));
    auto* video_filename_edit = new QLineEdit(&dialog);
    video_filename_edit->setPlaceholderText(QCoreApplication::translate("Usm.MediaBuildUi", "Source filename (converted extension when needed)"));
    video_filename_edit->setClearButtonEnabled(true);
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Video stream name"), video_filename_edit);

    auto* video_prep_combo = new QComboBox(&dialog);
    video_prep_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "Use prepared source"), static_cast<int>(MediaVideoPrep::UsePrepared));
    video_prep_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "FFmpeg VP9 elementary (.ivf)"), static_cast<int>(MediaVideoPrep::FfmpegVp9));
    video_prep_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "FFmpeg H.264 elementary (.264)"), static_cast<int>(MediaVideoPrep::FfmpegH264));
    video_prep_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "FFmpeg MPEG-1 elementary (.m1v)"), static_cast<int>(MediaVideoPrep::FfmpegMpeg1));
    video_prep_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "FFmpeg MPEG-2 elementary (.m2v)"), static_cast<int>(MediaVideoPrep::FfmpegMpeg2));
    video_prep_combo->setCurrentIndex(prefer_sfd ? 3 : 1);
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Video prep"), video_prep_combo);

    auto* video_preset_combo = new QComboBox(&dialog);
    video_preset_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "Compact"), static_cast<int>(MediaCodecPreset::Compact));
    video_preset_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "Standard"), static_cast<int>(MediaCodecPreset::Standard));
    video_preset_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "High quality"), static_cast<int>(MediaCodecPreset::HighQuality));
    video_preset_combo->setCurrentIndex(1);
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Video preset"), video_preset_combo);

    auto* alpha_edit = new QLineEdit(&dialog);
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Alpha video"), path_picker_row(
        dialog,
        *alpha_edit,
        QCoreApplication::translate("Usm.MediaBuildUi", "Browse"),
        QCoreApplication::translate("Usm.MediaBuildUi", "Choose alpha-video source"),
        PathPickerMode::OpenFile,
        QCoreApplication::translate("Usm.MediaBuildUi", "Video streams (*.ivf *.264 *.h264 *.m1v *.m2v *.mpg *.mpeg);;All files (*)")
    ));
    auto* alpha_filename_edit = new QLineEdit(&dialog);
    alpha_filename_edit->setPlaceholderText(QCoreApplication::translate("Usm.MediaBuildUi", "Source filename (converted extension when needed)"));
    alpha_filename_edit->setClearButtonEnabled(true);
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Alpha stream name"), alpha_filename_edit);
    auto* alpha_prep_combo = make_video_prep_combo(&dialog, MediaVideoPrep::UsePrepared);
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Alpha prep"), alpha_prep_combo);

    std::function<void()> refresh_validation;

    auto* audio_group = new QGroupBox(QCoreApplication::translate("Usm.MediaBuildUi", "Audio tracks"), &dialog);
    auto* audio_layout = new QVBoxLayout(audio_group);
    audio_layout->setContentsMargins(8, 8, 8, 8);
    audio_layout->setSpacing(6);
    std::vector<AudioTrackControls> audio_controls;
    auto* audio_codec_combo = new QComboBox(audio_group);
    audio_codec_combo->addItem(QStringLiteral("ADX"), static_cast<int>(MediaAudioPrep::FfmpegToAdx));
    audio_codec_combo->addItem(QStringLiteral("HCA"), static_cast<int>(MediaAudioPrep::FfmpegToHca));
    auto* audio_codec_row = new QHBoxLayout();
    audio_codec_row->addWidget(new QLabel(QCoreApplication::translate("Usm.MediaBuildUi", "Output codec"), audio_group), 0);
    audio_codec_row->addWidget(audio_codec_combo, 1);
    auto* audio_note = new QLabel(QCoreApplication::translate("Usm.MediaBuildUi", "No rows means no audio. Every row is converted to the selected codec unless it is already prepared."), audio_group);
    audio_note->setWordWrap(true);
    audio_layout->addLayout(audio_codec_row);
    audio_layout->addWidget(audio_note);
    QPushButton* add_audio_button = nullptr;
    auto add_audio_track = [&](QString source = {}) {
        const auto target = static_cast<MediaBuildTarget>(target_combo->currentData().toInt());
        const size_t limit = target == MediaBuildTarget::Sfd ? 1u : 256u;
        if (audio_controls.size() >= limit) {
            return;
        }
        AudioTrackControls controls;
        controls.row = new QWidget(audio_group);
        auto* row_layout = new QGridLayout(controls.row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setHorizontalSpacing(6);
        row_layout->setVerticalSpacing(4);
        controls.source = new QLineEdit(controls.row);
        controls.source->setText(std::move(source));
        row_layout->addWidget(path_picker_row(
            dialog,
            *controls.source,
            QCoreApplication::translate("Usm.MediaBuildUi", "Browse"),
            QCoreApplication::translate("Usm.MediaBuildUi", "Choose audio source"),
            PathPickerMode::OpenFile,
            audio_source_filter()
        ), 0, 0, 1, 5);
        row_layout->addWidget(new QLabel(QCoreApplication::translate("Usm.MediaBuildUi", "Stream name"), controls.row), 1, 0);
        controls.filename = new QLineEdit(controls.row);
        controls.filename->setPlaceholderText(QCoreApplication::translate("Usm.MediaBuildUi", "Source filename (converted extension when needed)"));
        controls.filename->setClearButtonEnabled(true);
        row_layout->addWidget(controls.filename, 1, 1, 1, 4);
        row_layout->addWidget(new QLabel(QCoreApplication::translate("Usm.MediaBuildUi", "Channel"), controls.row), 2, 0);
        controls.channel = channel_spin(controls.row);
        row_layout->addWidget(controls.channel, 2, 1);
        auto* remove = new QToolButton(controls.row);
        remove->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Remove"));
        row_layout->addWidget(remove, 2, 2);
        audio_layout->insertWidget(audio_layout->count() - 1, controls.row);
        audio_controls.push_back(controls);
        if (add_audio_button != nullptr) {
            add_audio_button->setEnabled(audio_controls.size() < limit);
        }

        QObject::connect(controls.source, &QLineEdit::textChanged, &dialog, [&](const QString&) {
            if (refresh_validation) refresh_validation();
        });
        QObject::connect(controls.filename, &QLineEdit::textChanged, &dialog, [&](const QString&) {
            if (refresh_validation) refresh_validation();
        });
        QObject::connect(controls.channel, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int) {
            if (refresh_validation) refresh_validation();
        });
        QObject::connect(remove, &QToolButton::clicked, &dialog, [&, row = controls.row] {
            std::erase_if(audio_controls, [row](const AudioTrackControls& item) { return item.row == row; });
            row->deleteLater();
            if (add_audio_button != nullptr) {
                const auto current_target = static_cast<MediaBuildTarget>(target_combo->currentData().toInt());
                const size_t current_limit = current_target == MediaBuildTarget::Sfd ? 1u : 256u;
                add_audio_button->setEnabled(audio_controls.size() < current_limit);
            }
            if (refresh_validation) refresh_validation();
        });
    };
    add_audio_button = new QPushButton(QCoreApplication::translate("Usm.MediaBuildUi", "Add audio track"), audio_group);
    audio_layout->addWidget(add_audio_button, 0, Qt::AlignLeft);
    QObject::connect(add_audio_button, &QPushButton::clicked, &dialog, [&] { add_audio_track(); });
    form->addRow(audio_group);

    auto* subtitle_group = new QGroupBox(QCoreApplication::translate("Usm.MediaBuildUi", "Subtitle tracks"), &dialog);
    auto* subtitle_layout = new QVBoxLayout(subtitle_group);
    subtitle_layout->setContentsMargins(8, 8, 8, 8);
    subtitle_layout->setSpacing(6);
    std::vector<SubtitleTrackControls> subtitle_controls;
    QPushButton* add_subtitle_button = nullptr;
    auto add_subtitle_track = [&](QString source = {}) {
        if (subtitle_controls.size() >= 256u) {
            return;
        }
        SubtitleTrackControls controls;
        controls.row = new QWidget(subtitle_group);
        auto* row_layout = new QGridLayout(controls.row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setHorizontalSpacing(6);
        row_layout->setVerticalSpacing(4);
        controls.source = new QLineEdit(controls.row);
        controls.source->setText(std::move(source));
        row_layout->addWidget(path_picker_row(
            dialog,
            *controls.source,
            QCoreApplication::translate("Usm.MediaBuildUi", "Browse"),
            QCoreApplication::translate("Usm.MediaBuildUi", "Choose subtitle source"),
            PathPickerMode::OpenFile,
            QCoreApplication::translate("Usm.MediaBuildUi", "Subtitles (*.srt *.ass *.ssa *.sbt *.txt);;All files (*)")
        ), 0, 0, 1, 7);
        row_layout->addWidget(new QLabel(QCoreApplication::translate("Usm.MediaBuildUi", "Stream name"), controls.row), 1, 0);
        controls.filename = new QLineEdit(controls.row);
        controls.filename->setPlaceholderText(QCoreApplication::translate("Usm.MediaBuildUi", ":source filename"));
        controls.filename->setClearButtonEnabled(true);
        row_layout->addWidget(controls.filename, 1, 1, 1, 6);
        controls.format = subtitle_format_combo(controls.row, cricodecs::usm::UsmSubtitleFormat::Auto);
        row_layout->addWidget(controls.format, 2, 0, 1, 2);
        row_layout->addWidget(new QLabel(QCoreApplication::translate("Usm.MediaBuildUi", "Language"), controls.row), 2, 2);
        controls.language = new QSpinBox(controls.row);
        controls.language->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        controls.language->setRange(0, (std::numeric_limits<int>::max)());
        controls.language->setMinimumWidth(112);
        row_layout->addWidget(controls.language, 2, 3);
        row_layout->addWidget(new QLabel(QCoreApplication::translate("Usm.MediaBuildUi", "Channel"), controls.row), 2, 4);
        controls.channel = channel_spin(controls.row);
        row_layout->addWidget(controls.channel, 2, 5);
        auto* remove = new QToolButton(controls.row);
        remove->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Remove"));
        row_layout->addWidget(remove, 2, 6);
        subtitle_layout->insertWidget(subtitle_layout->count() - 1, controls.row);
        subtitle_controls.push_back(controls);
        if (add_subtitle_button != nullptr) {
            add_subtitle_button->setEnabled(subtitle_controls.size() < 256u);
        }

        QObject::connect(controls.source, &QLineEdit::textChanged, &dialog, [&](const QString&) {
            if (refresh_validation) refresh_validation();
        });
        QObject::connect(controls.filename, &QLineEdit::textChanged, &dialog, [&](const QString&) {
            if (refresh_validation) refresh_validation();
        });
        QObject::connect(controls.format, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) {
            if (refresh_validation) refresh_validation();
        });
        QObject::connect(controls.language, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int) {
            if (refresh_validation) refresh_validation();
        });
        QObject::connect(controls.channel, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int) {
            if (refresh_validation) refresh_validation();
        });
        QObject::connect(remove, &QToolButton::clicked, &dialog, [&, row = controls.row] {
            std::erase_if(subtitle_controls, [row](const SubtitleTrackControls& item) { return item.row == row; });
            row->deleteLater();
            if (add_subtitle_button != nullptr) {
                add_subtitle_button->setEnabled(subtitle_controls.size() < 256u);
            }
            if (refresh_validation) refresh_validation();
        });
    };
    add_subtitle_button = new QPushButton(QCoreApplication::translate("Usm.MediaBuildUi", "Add subtitle track"), subtitle_group);
    subtitle_layout->addWidget(add_subtitle_button, 0, Qt::AlignLeft);
    QObject::connect(add_subtitle_button, &QPushButton::clicked, &dialog, [&] { add_subtitle_track(); });
    if (current_usm == nullptr) {
        add_subtitle_track();
    }
    form->addRow(subtitle_group);

    auto* sfd_profile_combo = new QComboBox(&dialog);
    sfd_profile_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "SofdecStream fixed 2048"), static_cast<int>(cricodecs::sfd::SfdBuildProfile::sofdec_stream_standard_fixed_2048));
    sfd_profile_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "SofdecStream2 v23249 fixed 2048"), static_cast<int>(cricodecs::sfd::SfdBuildProfile::sofdec_stream2_fixed_2048_v23249));
    sfd_profile_combo->addItem(QCoreApplication::translate("Usm.MediaBuildUi", "SofdecStream2 v23310 fixed 2048"), static_cast<int>(cricodecs::sfd::SfdBuildProfile::sofdec_stream2_fixed_2048_v23310));
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "SFD profile"), sfd_profile_combo);

    auto* output_edit = new QLineEdit(&dialog);
    output_edit->setText(safe_output_name(build_output_base_name(std::move(title)), prefer_sfd ? QStringLiteral(".sfd") : QStringLiteral(".usm")));
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Output"), path_picker_row(
        dialog, *output_edit, QCoreApplication::translate("Usm.MediaBuildUi", "Browse"),
        QCoreApplication::translate("Usm.MediaBuildUi", "Choose build output"), PathPickerMode::SaveFile,
        QCoreApplication::translate("Usm.MediaBuildUi", "CRI movie (*.usm *.sfd);;All files (*)")));

    std::vector<ExistingTrackControls> existing_controls;
    QCheckBox* apply_to_editor_check = nullptr;
    if (current_usm != nullptr) {
        video_edit->setEnabled(false);
        video_prep_combo->setEnabled(false);
        sfd_profile_combo->setEnabled(false);
        audio_group->setTitle(QCoreApplication::translate("Usm.MediaBuildUi", "Add audio streams"));
        audio_note->setText(QCoreApplication::translate(
            "Usm.MediaBuildUi",
            "Add ADX or HCA streams to the rebuilt USM. Existing streams remain controlled below."
        ));
        subtitle_group->setTitle(QCoreApplication::translate("Usm.MediaBuildUi", "Add subtitle streams"));

        auto* group = new QGroupBox(QCoreApplication::translate("Usm.MediaBuildUi", "Current USM streams"), &dialog);
        auto* stream_layout = new QVBoxLayout(group);
        stream_layout->setContentsMargins(10, 10, 10, 10);
        stream_layout->setSpacing(8);
        auto* note = value_label(QCoreApplication::translate("Usm.MediaBuildUi", "Choose a replacement only for streams you want to change. Empty replacements preserve the current stream."), group);
        note->setWordWrap(true);
        stream_layout->addWidget(note);
        for (size_t index = 0; index < current_usm->streams().size(); ++index) {
            const auto& stream = current_usm->streams()[index];
            auto* stream_group = new QGroupBox(group);
            auto* stream_form = new QFormLayout(stream_group);
            stream_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
            stream_form->setHorizontalSpacing(10);
            stream_form->setVerticalSpacing(6);
            ExistingTrackControls controls;
            controls.base.stream_index = static_cast<uint32_t>(index);
            controls.base.kind = stream_kind(stream.stream_id);
            controls.base.channel = stream.channel_no;
            controls.base.audio_codec = stream.audio_codec;
            controls.base.filename = stream.filename.empty() ? stream.filename_raw : stream.filename;
            const auto label_utf8 = stream_label(stream, static_cast<uint32_t>(index)).toUtf8();
            controls.base.label.assign(label_utf8.constData(), static_cast<size_t>(label_utf8.size()));

            controls.enabled = new QCheckBox(QCoreApplication::translate("Usm.MediaBuildUi", "Include this stream"), stream_group);
            controls.enabled->setChecked(controls.base.kind != MediaBuildConfig::ExistingUsmTrack::Kind::Unsupported);
            controls.enabled->setEnabled(controls.base.kind != MediaBuildConfig::ExistingUsmTrack::Kind::Unsupported);
            stream_group->setTitle(track_kind_text(controls.base.kind) + QCoreApplication::translate("Usm.MediaBuildUi", " stream %1").arg(index));
            stream_form->addRow(controls.enabled);
            stream_form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Current"), value_label(stream_label(stream, static_cast<uint32_t>(index)), stream_group));
            if (controls.base.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Unsupported) {
                auto* unsupported = value_label(
                    QCoreApplication::translate("Usm.MediaBuildUi", "This stream type is inspectable but not supported by the current builder."),
                    stream_group
                );
                unsupported->setWordWrap(true);
                stream_form->addRow(unsupported);
                stream_layout->addWidget(stream_group);
                existing_controls.push_back(std::move(controls));
                continue;
            }
            controls.filename = new QLineEdit(utf8_to_qstring(controls.base.filename), stream_group);
            controls.filename->setClearButtonEnabled(true);
            controls.filename->setToolTip(QCoreApplication::translate("Usm.MediaBuildUi", "Filename stored in the rebuilt USM stream metadata."));
            stream_form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Stream name"), controls.filename);
            controls.replacement = new QLineEdit(stream_group);
            const auto filter =
                (controls.base.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Video ||
                 controls.base.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Alpha)
                ? QCoreApplication::translate("Usm.MediaBuildUi", "Video streams (*.ivf *.264 *.h264 *.m1v *.m2v *.mpg *.mpeg);;All files (*)")
                : (controls.base.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Audio
                    ? audio_source_filter()
                    : QCoreApplication::translate("Usm.MediaBuildUi", "Subtitles (*.srt *.ass *.ssa *.sbt *.txt);;All files (*)"));
            stream_form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Replacement"), replacement_picker_row(dialog, *controls.replacement, QCoreApplication::translate("Usm.MediaBuildUi", "Choose replacement stream"), filter));

            if (controls.base.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Video ||
                controls.base.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Alpha) {
                controls.prep = make_video_prep_combo(stream_group, MediaVideoPrep::UsePrepared);
                stream_form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Replacement prep"), controls.prep);
            } else if (controls.base.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Audio) {
                controls.prep = make_audio_prep_combo(stream_group, MediaAudioPrep::UsePrepared);
                stream_form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Replacement prep"), controls.prep);
            } else if (controls.base.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Subtitle) {
                controls.prep = subtitle_format_combo(stream_group, cricodecs::usm::UsmSubtitleFormat::Auto);
                stream_form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Replacement format"), controls.prep);
                controls.language = new QSpinBox(stream_group);
                controls.language->setButtonSymbols(QAbstractSpinBox::PlusMinus);
                controls.language->setRange(0, (std::numeric_limits<int>::max)());
                controls.language->setValue(static_cast<int>(stream.channel_no));
                stream_form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Replacement language ID"), controls.language);
            }
            const auto sync_replacement_options = [enabled = controls.enabled, filename = controls.filename,
                                                    replacement = controls.replacement,
                                                    prep = controls.prep, language = controls.language] {
                const auto active = enabled->isChecked() && !replacement->text().trimmed().isEmpty();
                filename->setEnabled(enabled->isChecked());
                if (prep != nullptr) prep->setEnabled(active);
                if (language != nullptr) language->setEnabled(active);
            };
            QObject::connect(controls.enabled, &QCheckBox::toggled, &dialog, [sync_replacement_options](bool) { sync_replacement_options(); });
            QObject::connect(controls.replacement, &QLineEdit::textChanged, &dialog, [sync_replacement_options](const QString&) { sync_replacement_options(); });
            sync_replacement_options();
            stream_layout->addWidget(stream_group);
            existing_controls.push_back(std::move(controls));
        }
        content_layout->addWidget(group);
        apply_to_editor_check = new QCheckBox(QCoreApplication::translate("Usm.MediaBuildUi", "Replace this editor session after a successful build"), &dialog);
        apply_to_editor_check->setChecked(true);
        apply_to_editor_check->setToolTip(QCoreApplication::translate("Usm.MediaBuildUi", "Reload the built USM into this tab for immediate inspection."));
        content_layout->addWidget(apply_to_editor_check);
    }

    auto* ffmpeg_label = value_label(QString{}, &dialog);
    form->addRow(QStringLiteral("FFmpeg"), ffmpeg_label);
    auto ffmpeg_exe = find_ffmpeg_executable();
    ffmpeg_label->setText(ffmpeg_exe.isEmpty() ? QCoreApplication::translate("Usm.MediaBuildUi", "not found") : ffmpeg_exe);
    if (ffmpeg_exe.isEmpty()) {
        ffmpeg_label->setToolTip(QCoreApplication::translate("Usm.MediaBuildUi", "FFmpeg is required when selected video or audio preparation uses FFmpeg."));
    }

    auto* validation_label = value_label(QCoreApplication::translate("Usm.MediaBuildUi", "Ready. Choose source files and an output, then Build."), &dialog);
    validation_label->setMinimumWidth(500);
    form->addRow(QCoreApplication::translate("Usm.MediaBuildUi", "Validation"), validation_label);

    auto current_config = [&](bool include_source_bytes) {
        MediaBuildConfig config;
        config.target = static_cast<MediaBuildTarget>(target_combo->currentData().toInt());
        config.video_prep = static_cast<MediaVideoPrep>(video_prep_combo->currentData().toInt());
        config.alpha_prep = static_cast<MediaVideoPrep>(alpha_prep_combo->currentData().toInt());
        config.video_preset = static_cast<MediaCodecPreset>(video_preset_combo->currentData().toInt());
        config.sfd_profile = static_cast<cricodecs::sfd::SfdBuildProfile>(sfd_profile_combo->currentData().toInt());
        config.video_source = path_from_qstring(video_edit->text().trimmed());
        config.alpha_source = path_from_qstring(alpha_edit->text().trimmed());
        config.video_filename = qstring_to_utf8(video_filename_edit->text().trimmed());
        config.alpha_filename = qstring_to_utf8(alpha_filename_edit->text().trimmed());
        config.output_path = path_from_qstring(output_edit->text().trimmed());
        config.ffmpeg_path = path_from_qstring(ffmpeg_exe);
        config.keys = keys;
        static_cast<void>(apply_cri_key_text(
            config.keys,
            cri_key_edit->text(),
            cri_key_base->currentData().toInt()
        ));
        config.apply_to_editor = apply_to_editor_check == nullptr || apply_to_editor_check->isChecked();
        config.encrypt_audio = config.target == MediaBuildTarget::Usm && encrypt_audio_check->isChecked();
        const auto audio_target = static_cast<MediaAudioPrep>(audio_codec_combo->currentData().toInt());
        for (const auto& controls : audio_controls) {
            const auto source = path_from_qstring(controls.source->text().trimmed());
            if (source.empty()) {
                continue;
            }
            config.audio_tracks.push_back(MediaBuildConfig::AudioTrack{
                .source = source,
                .prep = audio_prep_for_source(source, audio_target),
                .channel_no = optional_channel(*controls.channel),
                .filename = qstring_to_utf8(controls.filename->text().trimmed()),
            });
        }
        if (config.target == MediaBuildTarget::Usm) {
            for (const auto& controls : subtitle_controls) {
                const auto source = path_from_qstring(controls.source->text().trimmed());
                if (source.empty()) {
                    continue;
                }
                config.subtitle_tracks.push_back(MediaBuildConfig::SubtitleTrack{
                    .source = source,
                    .format = static_cast<cricodecs::usm::UsmSubtitleFormat>(controls.format->currentData().toInt()),
                    .language_id = static_cast<uint32_t>(controls.language->value()),
                    .channel_no = optional_channel(*controls.channel),
                    .filename = qstring_to_utf8(controls.filename->text().trimmed()),
                });
            }
        }
        if (current_usm != nullptr) {
            config.existing_usm_path = current_usm_path;
            config.has_existing_usm_bytes = !current_usm_bytes.empty();
            if (include_source_bytes && config.existing_usm_path.empty()) {
                config.existing_usm_bytes.assign(current_usm_bytes.begin(), current_usm_bytes.end());
            }
            config.existing_usm_tracks.reserve(existing_controls.size());
            for (const auto& controls : existing_controls) {
                auto track = controls.base;
                track.enabled = controls.enabled == nullptr || controls.enabled->isChecked();
                track.filename = controls.filename == nullptr
                    ? track.filename
                    : qstring_to_utf8(controls.filename->text().trimmed());
                track.replacement_source = controls.replacement == nullptr
                    ? std::filesystem::path{}
                    : path_from_qstring(controls.replacement->text().trimmed());
                if ((track.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Video ||
                     track.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Alpha) &&
                    controls.prep != nullptr) {
                    track.video_prep = static_cast<MediaVideoPrep>(controls.prep->currentData().toInt());
                } else if (track.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Audio && controls.prep != nullptr) {
                    track.audio_prep = static_cast<MediaAudioPrep>(controls.prep->currentData().toInt());
                } else if (track.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Subtitle) {
                    if (controls.prep != nullptr) {
                        track.subtitle_format = static_cast<cricodecs::usm::UsmSubtitleFormat>(controls.prep->currentData().toInt());
                    }
                    if (controls.language != nullptr) {
                        track.subtitle_language_id = static_cast<uint32_t>(controls.language->value());
                    }
                }
                config.existing_usm_tracks.emplace_back(std::move(track));
            }
        }
        return config;
    };

    auto update_target_state = [&] {
        const auto target = static_cast<MediaBuildTarget>(target_combo->currentData().toInt());
        DecryptionKeys dialog_keys = keys;
        const auto key_error = apply_cri_key_text(
            dialog_keys,
            cri_key_edit->text(),
            cri_key_base->currentData().toInt()
        );
        const bool has_cri_key = !key_error.has_value() && dialog_keys.has_cri_key;
        sfd_profile_combo->setEnabled(target == MediaBuildTarget::Sfd);
        audio_group->setEnabled(true);
        subtitle_group->setVisible(target == MediaBuildTarget::Usm);
        form->setRowVisible(alpha_edit, target == MediaBuildTarget::Usm);
        form->setRowVisible(video_filename_edit, target == MediaBuildTarget::Usm && current_usm == nullptr);
        form->setRowVisible(alpha_filename_edit, target == MediaBuildTarget::Usm);
        form->setRowVisible(alpha_prep_combo, target == MediaBuildTarget::Usm);
        alpha_prep_combo->setEnabled(
            target == MediaBuildTarget::Usm && !alpha_edit->text().trimmed().isEmpty()
        );
        add_audio_button->setText(target == MediaBuildTarget::Usm
            ? QCoreApplication::translate("Usm.MediaBuildUi", "Add audio track")
            : QCoreApplication::translate("Usm.MediaBuildUi", "Add audio track (SFD supports one)"));
        add_audio_button->setEnabled(audio_controls.size() < (target == MediaBuildTarget::Sfd ? 1u : 256u));
        if (target == MediaBuildTarget::Sfd) {
            audio_codec_combo->setCurrentIndex(audio_codec_combo->findData(static_cast<int>(MediaAudioPrep::FfmpegToAdx)));
        }
        audio_codec_combo->setEnabled(target == MediaBuildTarget::Usm);
        const bool can_encrypt_audio = target == MediaBuildTarget::Usm && has_cri_key;
        if (!can_encrypt_audio) {
            encrypt_audio_check->setChecked(false);
        }
        encrypt_audio_check->setEnabled(can_encrypt_audio);
        video_prep_combo->setItemText(0, target == MediaBuildTarget::Sfd
            ? QCoreApplication::translate("Usm.MediaBuildUi", "Use prepared MPEG elementary stream")
            : QCoreApplication::translate("Usm.MediaBuildUi", "Use prepared source"));
        video_preset_combo->setEnabled(
            (current_usm == nullptr &&
             static_cast<MediaVideoPrep>(video_prep_combo->currentData().toInt()) != MediaVideoPrep::UsePrepared) ||
            (!alpha_edit->text().trimmed().isEmpty() &&
             static_cast<MediaVideoPrep>(alpha_prep_combo->currentData().toInt()) != MediaVideoPrep::UsePrepared)
        );
        if (target == MediaBuildTarget::Sfd &&
            (static_cast<MediaVideoPrep>(video_prep_combo->currentData().toInt()) == MediaVideoPrep::FfmpegH264 ||
             static_cast<MediaVideoPrep>(video_prep_combo->currentData().toInt()) == MediaVideoPrep::FfmpegVp9)) {
            video_prep_combo->setCurrentIndex(video_prep_combo->findData(static_cast<int>(MediaVideoPrep::FfmpegMpeg1)));
        }
        if (target == MediaBuildTarget::Sfd && output_edit->text().endsWith(QStringLiteral(".usm"), Qt::CaseInsensitive)) {
            auto output = output_edit->text();
            output.chop(4);
            output_edit->setText(output + QStringLiteral(".sfd"));
        } else if (target == MediaBuildTarget::Usm && output_edit->text().endsWith(QStringLiteral(".sfd"), Qt::CaseInsensitive)) {
            auto output = output_edit->text();
            output.chop(4);
            output_edit->setText(output + QStringLiteral(".usm"));
        } else if (!output_edit->text().contains(QLatin1Char('.'))) {
            output_edit->setText(safe_output_name(output_edit->text(), target == MediaBuildTarget::Sfd ? QStringLiteral(".sfd") : QStringLiteral(".usm")));
        }
    };
    QObject::connect(target_combo, &QComboBox::currentIndexChanged, &dialog, [update_target_state](int) { update_target_state(); });
    QObject::connect(video_prep_combo, &QComboBox::currentIndexChanged, &dialog, [update_target_state](int) { update_target_state(); });
    QObject::connect(alpha_prep_combo, &QComboBox::currentIndexChanged, &dialog, [update_target_state](int) { update_target_state(); });
    QObject::connect(alpha_edit, &QLineEdit::textChanged, &dialog, [update_target_state](const QString&) { update_target_state(); });
    QObject::connect(cri_key_edit, &QLineEdit::textChanged, &dialog, [update_target_state](const QString&) { update_target_state(); });
    QObject::connect(cri_key_base, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [update_target_state](int) { update_target_state(); });
    update_target_state();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* build_button = buttons->button(QDialogButtonBox::Ok);
    build_button->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Build"));
    layout->addWidget(buttons);

    refresh_validation = [&] {
        auto config = current_config(false);
        if (const auto error = apply_cri_key_text(
                config.keys,
                cri_key_edit->text(),
                cri_key_base->currentData().toInt())) {
            validation_label->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Blocked: %1").arg(*error));
            build_button->setEnabled(false);
            return;
        }
        if (const auto error = validate_media_build_config(config, false)) {
            validation_label->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Blocked: %1").arg(*error));
            build_button->setEnabled(false);
        } else {
            validation_label->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Ready to build."));
            build_button->setEnabled(true);
        }
    };
    const auto refresh_from_edit = [&refresh_validation](const QString&) { refresh_validation(); };
    for (auto* edit : {
             video_edit,
             video_filename_edit,
             alpha_edit,
             alpha_filename_edit,
             output_edit,
             cri_key_edit}) {
        QObject::connect(edit, &QLineEdit::textChanged, &dialog, refresh_from_edit);
    }
    QObject::connect(encrypt_audio_check, &QCheckBox::toggled, &dialog, [&refresh_validation](bool) {
        refresh_validation();
    });
    for (auto* combo : {
             video_prep_combo,
             alpha_prep_combo,
             video_preset_combo,
             sfd_profile_combo,
             audio_codec_combo,
             cri_key_base}) {
        QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&refresh_validation](int) {
            refresh_validation();
        });
    }
    for (const auto& controls : existing_controls) {
        QObject::connect(controls.enabled, &QCheckBox::toggled, &dialog, [&refresh_validation](bool) {
            refresh_validation();
        });
        if (controls.replacement != nullptr) {
            QObject::connect(controls.replacement, &QLineEdit::textChanged, &dialog, refresh_from_edit);
        }
        if (controls.filename != nullptr) {
            QObject::connect(controls.filename, &QLineEdit::textChanged, &dialog, refresh_from_edit);
        }
        if (controls.prep != nullptr) {
            QObject::connect(controls.prep, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&refresh_validation](int) {
                refresh_validation();
            });
        }
        if (controls.language != nullptr) {
            QObject::connect(controls.language, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&refresh_validation](int) {
                refresh_validation();
            });
        }
    }
    refresh_validation();
    QObject::connect(build_button, &QPushButton::clicked, &dialog, [&] {
        auto config = current_config(false);
        if (const auto error = apply_cri_key_text(
                config.keys,
                cri_key_edit->text(),
                cri_key_base->currentData().toInt())) {
            validation_label->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Blocked: %1").arg(*error));
            validation_label->setFocus();
            return;
        }
        if (const auto error = validate_media_build_config(config, false)) {
            validation_label->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Blocked: %1").arg(*error));
            validation_label->setFocus();
            return;
        }
        validation_label->setText(QCoreApplication::translate("Usm.MediaBuildUi", "Validation passed. Starting background build."));
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<MediaBuildConfig>{};
    }

    MediaBuildConfig config = current_config(true);
    if (const auto error = apply_cri_key_text(
            config.keys,
            cri_key_edit->text(),
            cri_key_base->currentData().toInt())) {
        return std::unexpected(*error);
    }
    if (const auto error = validate_media_build_config(config, false)) {
        return std::unexpected(*error);
    }
    return std::optional<MediaBuildConfig>(std::move(config));
}

} // namespace cristudio::modules::usm
