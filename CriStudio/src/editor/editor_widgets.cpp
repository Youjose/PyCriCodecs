#include "editor/editor_widgets.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVideoWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace cristudio {

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QCoreApplication::translate("Editor.EditorWidgets", "Toggle switch"));
}

QSize ToggleSwitch::sizeHint() const {
    return {46, 24};
}

QSize ToggleSwitch::minimumSizeHint() const {
    return sizeHint();
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto track = rect().adjusted(1, 2, -1, -2);
    const auto track_color = isChecked()
        ? palette().color(QPalette::Highlight)
        : palette().color(QPalette::Mid);
    painter.setPen(Qt::NoPen);
    painter.setBrush(track_color);
    painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);

    constexpr int margin = 3;
    const int diameter = track.height() - margin * 2;
    const int x = isChecked()
        ? track.right() - margin - diameter + 1
        : track.left() + margin;
    painter.setBrush(isChecked()
        ? palette().color(QPalette::HighlightedText)
        : palette().color(QPalette::ButtonText));
    painter.drawEllipse(QRect(x, track.top() + margin, diameter, diameter));

    if (hasFocus()) {
        QStyleOptionFocusRect focus;
        focus.initFrom(this);
        focus.rect = rect();
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
    }
}

void SeekSlider::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QSlider::mousePressEvent(event);
        return;
    }
    event->accept();
    set_value_from_position(event->position().x());
    emit sliderPressed();
    emit sliderMoved(value());
}

void SeekSlider::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        QSlider::mouseMoveEvent(event);
        return;
    }
    event->accept();
    set_value_from_position(event->position().x());
    emit sliderMoved(value());
}

void SeekSlider::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QSlider::mouseReleaseEvent(event);
        return;
    }
    event->accept();
    set_value_from_position(event->position().x());
    emit sliderReleased();
}

void SeekSlider::set_value_from_position(qreal x) {
    const auto bounded_x = std::clamp(x, 0.0, static_cast<double>(width()));
    setValue(QStyle::sliderValueFromPosition(
        minimum(), maximum(), static_cast<int>(bounded_x), (std::max)(1, width()), invertedAppearance()));
}

VideoDisplay make_video_display(QWidget* parent) {
    VideoDisplay display;
    display.frame = new QWidget(parent);
    display.frame->setObjectName(QStringLiteral("VideoFrame"));
    display.frame->setMinimumHeight(260);
    display.frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(display.frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    display.widget = new QVideoWidget(display.frame);
    display.widget->setMinimumHeight(260);
    display.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    display.widget->setAspectRatioMode(Qt::KeepAspectRatio);
    layout->addWidget(display.widget);
    display.frame->hide();
    return display;
}

MediaControls make_media_controls(QWidget* parent) {
    MediaControls controls;
    controls.panel = new QWidget(parent);
    controls.panel->setObjectName(QStringLiteral("AudioPanel"));
    controls.panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* layout = new QVBoxLayout(controls.panel);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    const auto selector_row = [&](QWidget*& row, QLabel*& label, QComboBox*& combo,
                                  QToolButton*& popup, const char* title, const char* object_name) {
        row = new QWidget(controls.panel);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(8);
        label = dim_label(QString::fromLatin1(title), row);
        combo = new QComboBox(row);
        combo->setObjectName(QString::fromLatin1(object_name));
        popup = new QToolButton(row);
        popup->setArrowType(Qt::DownArrow);
        row_layout->addWidget(label);
        row_layout->addWidget(combo, 1);
        row_layout->addWidget(popup);
        QObject::connect(popup, &QToolButton::clicked, combo, &QComboBox::showPopup);
        row->hide();
        layout->addWidget(row);
    };
    selector_row(controls.audio_row, controls.audio_label, controls.audio_combo,
                 controls.audio_popup, "Audio channel", "MuxAudioCombo");
    selector_row(controls.subtitle_row, controls.subtitle_label, controls.subtitle_combo,
                 controls.subtitle_popup, "Subtitles", "MuxSubtitleCombo");

    auto* playback = new QHBoxLayout();
    playback->setContentsMargins(0, 0, 0, 0);
    playback->setSpacing(8);
    controls.play_button = new QToolButton(controls.panel);
    controls.play_button->setIcon(controls.panel->style()->standardIcon(QStyle::SP_MediaPlay));
    controls.play_button->setText(QStringLiteral("Play"));
    controls.play_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    controls.play_button->setEnabled(false);
    controls.status_label = new QLabel(QStringLiteral("No playable media selected"), controls.panel);
    controls.status_label->setObjectName(QStringLiteral("AudioStatus"));
    controls.status_label->setWordWrap(true);
    controls.volume_label = new QLabel(controls.panel);
    controls.volume_label->setPixmap(controls.panel->style()->standardIcon(QStyle::SP_MediaVolume).pixmap(16, 16));
    controls.volume_slider = new QSlider(Qt::Horizontal, controls.panel);
    controls.volume_slider->setObjectName(QStringLiteral("VolumeSlider"));
    controls.volume_slider->setRange(0, 100);
    controls.volume_slider->setValue(80);
    controls.volume_slider->setFixedWidth(96);
    playback->addWidget(controls.play_button);
    playback->addWidget(controls.status_label, 1);
    playback->addWidget(controls.volume_label, 0, Qt::AlignVCenter);
    playback->addWidget(controls.volume_slider, 0, Qt::AlignVCenter);
    layout->addLayout(playback);

    controls.loop_row = new QWidget(controls.panel);
    auto* loop_layout = new QVBoxLayout(controls.loop_row);
    loop_layout->setContentsMargins(0, 0, 0, 0);
    loop_layout->setSpacing(4);
    controls.loop_toggle = new QCheckBox(QStringLiteral("Loop selected range"), controls.loop_row);
    controls.loop_toggle->setEnabled(false);
    controls.loop_list = new QListWidget(controls.loop_row);
    controls.loop_list->setObjectName(QStringLiteral("LoopList"));
    controls.loop_list->setEnabled(false);
    controls.loop_list->setSelectionMode(QAbstractItemView::SingleSelection);
    controls.loop_list->setAlternatingRowColors(false);
    controls.loop_list->setUniformItemSizes(false);
    controls.loop_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controls.loop_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    controls.loop_list->setMinimumHeight(42);
    controls.loop_list->setMaximumHeight(96);
    loop_layout->addWidget(controls.loop_toggle);
    loop_layout->addWidget(controls.loop_list);
    controls.loop_row->hide();
    layout->addWidget(controls.loop_row);

    auto* progress = new QHBoxLayout();
    progress->setContentsMargins(0, 0, 0, 0);
    progress->setSpacing(8);
    controls.seek_slider = new SeekSlider(Qt::Horizontal, controls.panel);
    controls.seek_slider->setRange(0, 0);
    controls.seek_slider->setEnabled(false);
    controls.time_label = new QLabel(QStringLiteral("0:00 / 0:00"), controls.panel);
    controls.time_label->setMinimumWidth(92);
    controls.time_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progress->addWidget(controls.seek_slider, 1);
    progress->addWidget(controls.time_label);
    layout->addLayout(progress);
    return controls;
}

QLabel* dim_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setObjectName(QStringLiteral("DimLabel"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QLabel* value_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setObjectName(QStringLiteral("ValueLabel"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

QWidget* path_picker_row(
    QWidget& parent,
    QLineEdit& edit,
    QString button_text,
    QString title,
    PathPickerMode mode,
    QString filter
) {
    edit.setClearButtonEnabled(true);
    auto* row = new QWidget(&parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(&edit, 1);
    auto* browse = new QPushButton(std::move(button_text), row);
    layout->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, &parent, [&parent, &edit, title = std::move(title), mode, filter = std::move(filter)] {
        QString selected;
        switch (mode) {
        case PathPickerMode::OpenFile:
            selected = QFileDialog::getOpenFileName(&parent, title, edit.text(), filter);
            break;
        case PathPickerMode::SaveFile:
            selected = QFileDialog::getSaveFileName(&parent, title, edit.text(), filter);
            break;
        case PathPickerMode::Directory:
            selected = QFileDialog::getExistingDirectory(&parent, title, edit.text());
            break;
        }
        if (!selected.isEmpty()) {
            edit.setText(selected);
        }
    });
    return row;
}

QDialogButtonBox* dialog_buttons(QDialog& dialog, QString accept_text) {
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (!accept_text.isEmpty()) {
        buttons->button(QDialogButtonBox::Ok)->setText(std::move(accept_text));
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return buttons;
}

void bind_valid_inputs(QPushButton& accept, std::initializer_list<QLineEdit*> inputs) {
    const QList<QLineEdit*> edits(inputs);
    const auto refresh = [&accept, edits] {
        accept.setEnabled(std::ranges::all_of(
            edits, [](const QLineEdit* edit) { return edit->hasAcceptableInput(); }));
    };
    for (auto* edit : edits) {
        QObject::connect(edit, &QLineEdit::textChanged, &accept, [refresh](const QString&) {
            refresh();
        });
    }
    refresh();
}

void add_editor_start_tab(QTabWidget* tabs) {
    auto* empty = new QLabel(QCoreApplication::translate("Editor.EditorWidgets", "Open files or archive entries in the Editor from the browser context menus."), tabs);
    empty->setObjectName(QStringLiteral("DocumentSubtitle"));
    empty->setAlignment(Qt::AlignCenter);
    tabs->addTab(empty, QCoreApplication::translate("Editor.EditorWidgets", "Start"));
    tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
}

void retranslate_editor_start_tab(QTabWidget* tabs) {
    if (tabs == nullptr || tabs->count() != 1) {
        return;
    }
    auto* empty = qobject_cast<QLabel*>(tabs->widget(0));
    if (empty == nullptr) {
        return;
    }
    empty->setText(QCoreApplication::translate("Editor.EditorWidgets", "Open files or archive entries in the Editor from the browser context menus."));
    tabs->setTabText(0, QCoreApplication::translate("Editor.EditorWidgets", "Start"));
}

void remove_editor_tab(QTabWidget* tabs, QWidget* widget) {
    if (tabs == nullptr || widget == nullptr) {
        return;
    }
    const auto index = tabs->indexOf(widget);
    if (index < 0) {
        return;
    }
    tabs->removeTab(index);
    widget->deleteLater();
    if (tabs->count() == 0) {
        add_editor_start_tab(tabs);
    }
}

} // namespace cristudio
