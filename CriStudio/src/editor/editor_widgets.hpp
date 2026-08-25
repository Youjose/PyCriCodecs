#pragma once

#include <QAbstractButton>
#include <QSlider>
#include <QString>

#include <initializer_list>

class QDialog;
class QDialogButtonBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QSize;
class QTabWidget;
class QToolButton;
class QVideoWidget;
class QWidget;

namespace cristudio {

class ToggleSwitch final : public QAbstractButton {
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

class SeekSlider final : public QSlider {
public:
    using QSlider::QSlider;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void set_value_from_position(qreal x);
};

struct MediaControls {
    QWidget* panel = nullptr;
    QWidget* audio_row = nullptr;
    QLabel* audio_label = nullptr;
    QComboBox* audio_combo = nullptr;
    QToolButton* audio_popup = nullptr;
    QWidget* subtitle_row = nullptr;
    QLabel* subtitle_label = nullptr;
    QComboBox* subtitle_combo = nullptr;
    QToolButton* subtitle_popup = nullptr;
    QToolButton* play_button = nullptr;
    QLabel* status_label = nullptr;
    QLabel* volume_label = nullptr;
    QSlider* volume_slider = nullptr;
    QWidget* loop_row = nullptr;
    QCheckBox* loop_toggle = nullptr;
    QListWidget* loop_list = nullptr;
    SeekSlider* seek_slider = nullptr;
    QLabel* time_label = nullptr;
};

struct VideoDisplay {
    QWidget* frame = nullptr;
    QVideoWidget* widget = nullptr;
};

[[nodiscard]] VideoDisplay make_video_display(QWidget* parent);
[[nodiscard]] MediaControls make_media_controls(QWidget* parent);

[[nodiscard]] QLabel* dim_label(QString text, QWidget* parent = nullptr);
[[nodiscard]] QLabel* value_label(QString text, QWidget* parent = nullptr);

enum class PathPickerMode {
    OpenFile,
    SaveFile,
    Directory
};

[[nodiscard]] QWidget* path_picker_row(
    QWidget& parent,
    QLineEdit& edit,
    QString button_text,
    QString title,
    PathPickerMode mode,
    QString filter = {});

[[nodiscard]] QDialogButtonBox* dialog_buttons(QDialog& dialog, QString accept_text = {});
void bind_valid_inputs(QPushButton& accept, std::initializer_list<QLineEdit*> inputs);

void add_editor_start_tab(QTabWidget* tabs);
void retranslate_editor_start_tab(QTabWidget* tabs);
void remove_editor_tab(QTabWidget* tabs, QWidget* widget);

} // namespace cristudio
