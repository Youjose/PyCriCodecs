#pragma once

#include "editor/editor_widgets.hpp"

#include <QCoreApplication>
#include <QIcon>
#include <QElapsedTimer>
#include <QPalette>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <key_recovery.hpp>

class QLabel;
class QToolButton;
class QWidget;

namespace cristudio {

inline constexpr size_t MaxInterimKeyRecoveryGroups = 64;

struct KeyRecoveryGroup {
    uint64_t identity = 0;
    QString key;
    float mean_score = 0.0f;
    size_t count = 0;
    QStringList files;
    size_t omitted_files = 0;
    size_t recommended_count = 0;
};

struct KeyRecoveryCandidate {
    uint64_t identity = 0;
    QString key;
    float score = 0.0f;
    QString file;
    bool recommended = false;
};

[[nodiscard]] std::vector<KeyRecoveryGroup> group_key_recovery_candidates(
    std::span<const KeyRecoveryCandidate> candidates,
    size_t max_groups = 0);
[[nodiscard]] bool show_key_recovery_candidate(float score, float best_score) noexcept;

class KeyRecoveryProgressThrottle {
public:
    [[nodiscard]] bool ready(size_t completed, size_t total);

private:
    QElapsedTimer m_timer;
    size_t m_last_completed = 0;
};

enum class ActionGlyph {
    Extract,
    RawExtract,
    RecoverKey,
    Clear,
    MuxPreview,
};

QString archive_basename(QString text);
void reveal_in_file_manager(const QString& path);
QString strip_mux_prefix(QString text);
[[nodiscard]] QString recovery_key_text(uint64_t key, int digits);
[[nodiscard]] QString recovery_source_label(
    std::string_view name,
    const std::filesystem::path& path,
    const char* translation_context);

QPalette dark_palette();
QPalette light_palette();
QString visual_stylesheet(bool dark);
QString app_title();

QIcon make_sidebar_icon(bool panel_on_left);
QIcon make_action_icon(ActionGlyph glyph);
QToolButton* make_panel_button(const QIcon& icon, const QString& tooltip, QWidget* parent);
QToolButton* make_rail_action_button(const QIcon& icon, const QString& tooltip, QWidget* parent);
void fade_widget_in(QWidget* widget, int duration_ms = 140);
[[nodiscard]] std::optional<cricodecs::KeyRecoveryMode> choose_key_recovery_mode(
    QWidget* parent,
    QString title,
    size_t source_count);
void begin_key_recovery_progress(
    QWidget* parent,
    QString title,
    size_t source_count,
    std::function<void()> cancel = {});
void update_key_recovery_progress(
    QWidget* parent,
    std::vector<KeyRecoveryGroup> groups,
    size_t completed,
    size_t total,
    QString status,
    bool finished = false);
void show_key_recovery_result(
    QWidget* parent,
    QString title,
    QString summary,
    QString details,
    QString copy_text = {},
    QString copy_button_text = QCoreApplication::translate("MainWindow.UiHelpers", "Copy Key"),
    std::vector<KeyRecoveryGroup> groups = {}
);
void show_key_recovery_error(QWidget* parent, QString title, QString error);

} // namespace cristudio
