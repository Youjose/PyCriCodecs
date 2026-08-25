#include "../main_window.hpp"

#include "../browser/browser_delegates.hpp"
#include "../editor/hex_preview_widget.hpp"
#include "../editor_workspace.hpp"
#include "key_panel.hpp"
#include "preview_helpers.hpp"
#include "ui_helpers.hpp"
#include "../path_text.hpp"
#include "../shared/translation_manager.hpp"

#include <cricodecs/version.hpp>

#include <QCoreApplication>
#include <QAbstractAnimation>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenuBar>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVariantAnimation>
#include <QVideoWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>

#if defined(Q_OS_WIN) || defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
#if defined(Q_OS_LINUX) || defined(__linux__)
#include <unistd.h>
#endif
#if defined(Q_OS_MACOS) || defined(__APPLE__)
#include <mach/mach.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace cristudio {
namespace {

constexpr std::string_view mux_preview_prefix = "Mux preview/";

class FileFilterProxyModel final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void set_search_text(QString text) {
        text = text.trimmed();
        if (m_search_text == text) {
            return;
        }
        beginFilterChange();
        m_search_text = std::move(text);
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

    void set_type_filters(QStringList types) {
        types.sort(Qt::CaseInsensitive);
        types.removeDuplicates();
        if (m_type_filters == types) {
            return;
        }
        beginFilterChange();
        m_type_filters = std::move(types);
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        const auto source_index = sourceModel()->index(source_row, 0, source_parent);
        if (!source_index.isValid()) {
            return false;
        }
        if (!m_type_filters.isEmpty() && !m_type_filters.contains(source_index.data(FileListModel::FilterFormatRole).toString(), Qt::CaseInsensitive)) {
            return false;
        }
        if (m_search_text.isEmpty()) {
            return true;
        }
        return source_index.data(FileListModel::SearchRole)
            .toString()
            .contains(m_search_text, Qt::CaseInsensitive);
    }

private:
    QString m_search_text;
    QStringList m_type_filters;
};

class FileTypeFilterCombo final : public QComboBox {
public:
    using QComboBox::QComboBox;

    void set_selection_changed(std::function<void()> callback) {
        m_selection_changed = std::move(callback);
    }

protected:
    void showPopup() override {
        QComboBox::showPopup();
        if (view() != nullptr && view()->viewport() != nullptr) {
            view()->viewport()->installEventFilter(this);
        }
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (view() != nullptr
            && watched == view()->viewport()
            && event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const auto index = view()->indexAt(mouse->position().toPoint());
            if (!index.isValid()) {
                return QComboBox::eventFilter(watched, event);
            }

            if (index.row() == 0) {
                for (int item = 1; item < count(); ++item) {
                    setItemData(item, Qt::Unchecked, Qt::CheckStateRole);
                }
            } else {
                const auto state = itemData(index.row(), Qt::CheckStateRole).value<Qt::CheckState>();
                setItemData(index.row(), state == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
            }

            if (m_selection_changed) {
                m_selection_changed();
            }
            return true;
        }
        return QComboBox::eventFilter(watched, event);
    }

private:
    std::function<void()> m_selection_changed;
};

QStringList selected_file_type_filters(const QComboBox* combo) {
    QStringList selected;
    if (combo == nullptr) {
        return selected;
    }
    for (int index = 1; index < combo->count(); ++index) {
        if (combo->itemData(index, Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked) {
            selected.push_back(combo->itemData(index).toString());
        }
    }
    return selected;
}

void update_file_type_filter_label(QComboBox* combo) {
    if (combo == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    const auto selected = selected_file_type_filters(combo);
    const auto single_label = [&]() {
        if (selected.size() != 1) {
            return QString{};
        }
        for (int index = 1; index < combo->count(); ++index) {
            if (combo->itemData(index).toString().compare(selected.front(), Qt::CaseInsensitive) == 0) {
                return combo->itemText(index);
            }
        }
        return selected.front();
    }();
    const auto label = selected.isEmpty()
        ? QCoreApplication::translate("MainWindow.Chrome", "All types")
        : (selected.size() == 1 ? single_label : QCoreApplication::translate("MainWindow.Chrome", "%1 types").arg(selected.size()));
    combo->setItemText(0, label);
    combo->setItemData(0, selected.isEmpty() ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    combo->setCurrentIndex(0);
}

void refresh_file_type_filter(QComboBox* combo, const FileListModel* model) {
    if (combo == nullptr || model == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    const auto previous = selected_file_type_filters(combo);
    std::vector<std::pair<QString, QString>> formats;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        const auto id = index.data(FileListModel::FilterFormatRole).toString();
        const auto label = index.data(FileListModel::FilterFormatLabelRole).toString();
        if (
            !id.isEmpty() &&
            std::ranges::none_of(formats, [&id](const auto& item) {
                return item.first.compare(id, Qt::CaseInsensitive) == 0;
            })
        ) {
            formats.emplace_back(id, label);
        }
    }
    std::ranges::sort(formats, [](const auto& left, const auto& right) {
        return left.second.compare(right.second, Qt::CaseInsensitive) < 0;
    });

    combo->clear();
    combo->addItem(QCoreApplication::translate("MainWindow.Chrome", "All types"), QString{});
    combo->setItemData(0, Qt::Checked, Qt::CheckStateRole);
    for (const auto& [id, label] : formats) {
        combo->addItem(label, id);
        combo->setItemData(combo->count() - 1, previous.contains(id, Qt::CaseInsensitive) ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    }
    update_file_type_filter_label(combo);
}

void sync_segment_buttons(QWidget* selector, int active) {
    if (selector == nullptr) {
        return;
    }
    const auto buttons = selector->findChildren<QToolButton*>(QStringLiteral("EntryViewModeSegment"));
    for (auto* button : buttons) {
        const QSignalBlocker blocker(button);
        button->setChecked(button->property("modeValue").toInt() == active);
    }
}

class ColumnSplitterHandle final : public QSplitterHandle {
public:
    explicit ColumnSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
        : QSplitterHandle(orientation, parent) {
        setCursor(orientation == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QSplitterHandle::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const auto line = palette().mid().color();
        auto grip = line;
        grip.setAlpha(150);
        painter.setBrush(line);
        if (orientation() == Qt::Horizontal) {
            const auto x = width() / 2;
            painter.drawRect(QRect(x, 0, 1, height()));
            painter.setBrush(grip);
            const auto center = rect().center();
            for (int i = -1; i <= 1; ++i) {
                painter.drawRoundedRect(QRectF(center.x() - 1.5, center.y() + (i * 7) - 1.5, 3.0, 3.0), 1.5, 1.5);
            }
        } else {
            const auto y = height() / 2;
            painter.drawRect(QRect(0, y, width(), 1));
            painter.setBrush(grip);
            const auto center = rect().center();
            for (int i = -1; i <= 1; ++i) {
                painter.drawRoundedRect(QRectF(center.x() + (i * 7) - 1.5, center.y() - 1.5, 3.0, 3.0), 1.5, 1.5);
            }
        }
    }
};

class ColumnSplitter final : public QSplitter {
public:
    using QSplitter::QSplitter;

protected:
    QSplitterHandle* createHandle() override {
        return new ColumnSplitterHandle(orientation(), this);
    }
};

class AutoHideRail final : public QWidget {
public:
    explicit AutoHideRail(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_Hover);
        setFixedWidth(m_hidden_width);
        m_hide_timer.setSingleShot(true);
        m_hide_timer.setInterval(220);
        QObject::connect(&m_hide_timer, &QTimer::timeout, this, [this] {
            if (underMouse() || (m_hover_guard != nullptr && m_hover_guard->underMouse())) {
                m_hide_timer.start();
                return;
            }
            conceal();
        });
        m_animation.setDuration(140);
        m_animation.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            setFixedWidth(value.toInt());
            updateGeometry();
        });
    }

public:
    [[nodiscard]] bool expanded() const noexcept { return m_expanded; }

    void reveal() {
        set_expanded(true);
    }

    void conceal() {
        if (!m_auto_hide_enabled) {
            return;
        }
        set_expanded(false);
    }

    void set_auto_hide_enabled(bool enabled) {
        m_auto_hide_enabled = enabled;
        if (!enabled) {
            m_hide_timer.stop();
            reveal();
        } else if (!underMouse()) {
            conceal();
        }
    }

    void set_expanded_changed(std::function<void(bool)> callback) {
        m_expanded_changed = std::move(callback);
    }

    void set_hover_guard(QWidget* widget) {
        m_hover_guard = widget;
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        QWidget::enterEvent(event);
        m_hide_timer.stop();
        reveal();
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        m_hide_timer.start();
    }

private:
    void set_expanded(bool expanded) {
        const auto target = expanded ? m_shown_width : m_hidden_width;
        if (m_expanded == expanded && width() == target && m_animation.state() != QAbstractAnimation::Running) {
            return;
        }
        m_expanded = expanded;
        if (m_expanded_changed) {
            m_expanded_changed(expanded);
        }
        m_animation.stop();
        m_animation.setStartValue(width());
        m_animation.setEndValue(target);
        m_animation.start();
    }

    static constexpr int m_hidden_width = 0;
    static constexpr int m_shown_width = 42;
    bool m_auto_hide_enabled = true;
    bool m_expanded = false;
    QTimer m_hide_timer;
    QVariantAnimation m_animation;
    std::function<void(bool)> m_expanded_changed;
    QWidget* m_hover_guard = nullptr;
};

class AutoHideTabBar final : public QTabBar {
public:
    explicit AutoHideTabBar(QWidget* parent = nullptr)
        : QTabBar(parent) {
        setObjectName(QStringLiteral("WorkspaceShelfTabs"));
        setAttribute(Qt::WA_Hover);
        setExpanding(false);
        setUsesScrollButtons(false);
        setMouseTracking(true);
        setFixedHeight(m_hidden_height);
        m_hide_timer.setSingleShot(true);
        m_hide_timer.setInterval(260);
        QObject::connect(&m_hide_timer, &QTimer::timeout, this, [this] {
            conceal();
        });
        m_animation.setDuration(150);
        m_animation.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            setFixedHeight(value.toInt());
            updateGeometry();
        });
    }

    [[nodiscard]] QSize sizeHint() const override {
        auto size = QTabBar::sizeHint();
        size.setHeight(height());
        return size;
    }

    [[nodiscard]] QSize minimumSizeHint() const override {
        auto size = QTabBar::minimumSizeHint();
        size.setHeight(height());
        return size;
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        QTabBar::enterEvent(event);
        m_hide_timer.stop();
        reveal();
    }

    void leaveEvent(QEvent* event) override {
        QTabBar::leaveEvent(event);
        m_hide_timer.start();
    }

    void focusInEvent(QFocusEvent* event) override {
        QTabBar::focusInEvent(event);
        m_hide_timer.stop();
        reveal();
    }

    void focusOutEvent(QFocusEvent* event) override {
        QTabBar::focusOutEvent(event);
        conceal();
    }

public:
    [[nodiscard]] bool expanded() const noexcept { return m_expanded; }

    void reveal() {
        set_expanded(true);
    }

    void conceal() {
        if (!m_auto_hide_enabled) {
            return;
        }
        set_expanded(false);
    }

    void set_auto_hide_enabled(bool enabled) {
        m_auto_hide_enabled = enabled;
        if (!enabled) {
            m_hide_timer.stop();
            reveal();
        }
    }

    void set_expanded_changed(std::function<void(bool)> callback) {
        m_expanded_changed = std::move(callback);
    }

private:
    void set_expanded(bool expanded) {
        const auto target = expanded ? m_shown_height : m_hidden_height;
        if (m_expanded == expanded && height() == target && m_animation.state() != QAbstractAnimation::Running) {
            return;
        }
        m_expanded = expanded;
        if (m_expanded_changed) {
            m_expanded_changed(expanded);
        }
        m_animation.stop();
        m_animation.setStartValue(height());
        m_animation.setEndValue(target);
        m_animation.start();
    }

    static constexpr int m_hidden_height = 0;
    static constexpr int m_shown_height = 30;
    bool m_auto_hide_enabled = true;
    bool m_expanded = false;
    QTimer m_hide_timer;
    QVariantAnimation m_animation;
    std::function<void(bool)> m_expanded_changed;
};

class ContentHostWidget final : public QWidget {
public:
    using QWidget::QWidget;

    void set_edge_hover_bands(QWidget* left, QWidget* right) {
        m_left_hover_band = left;
        m_right_hover_band = right;
        position_edge_hover_bands();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        position_edge_hover_bands();
    }

private:
    void position_edge_hover_bands() {
        if (m_left_hover_band != nullptr) {
            m_left_hover_band->setGeometry(0, 0, 30, height());
            m_left_hover_band->raise();
        }
        if (m_right_hover_band != nullptr) {
            m_right_hover_band->setGeometry((std::max)(0, width() - 30), 0, 30, height());
            m_right_hover_band->raise();
        }
    }

    QWidget* m_left_hover_band = nullptr;
    QWidget* m_right_hover_band = nullptr;
};

class EdgeHoverBand final : public QWidget {
public:
    explicit EdgeHoverBand(std::function<void()> callback, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_callback(std::move(callback)) {
        setObjectName(QStringLiteral("EdgeHoverBand"));
        setMouseTracking(true);
        setAttribute(Qt::WA_StyledBackground, true);
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        QWidget::enterEvent(event);
        if (m_callback) {
            m_callback();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        QWidget::mouseMoveEvent(event);
        if (m_callback) {
            m_callback();
        }
    }

private:
    std::function<void()> m_callback;
};

class WorkspaceTabWidget final : public QTabWidget {
public:
    explicit WorkspaceTabWidget(QWidget* parent = nullptr)
        : QTabWidget(parent) {
        if (auto* app = QApplication::instance(); app != nullptr) {
            app->installEventFilter(this);
        }
        m_tabs = new AutoHideTabBar(this);
        setTabBar(m_tabs);
        m_hover_band = new QWidget(this);
        m_hover_band->setObjectName(QStringLiteral("WorkspaceShelfHoverBand"));
        m_hover_band->setMouseTracking(true);
        m_hover_band->installEventFilter(this);
        m_tabs->set_expanded_changed([this](bool expanded) {
            if (m_hover_band == nullptr) {
                return;
            }
            m_hover_band->setVisible(!m_ribbon_locked && currentIndex() == 0 && !expanded);
            if (expanded && currentIndex() == 0) {
                start_top_hide_watch();
            } else if (!expanded && m_top_hide_timer != nullptr) {
                m_top_hide_timer->stop();
            }
            if (!expanded) {
                m_hover_band->raise();
            }
        });
        m_top_hide_timer = new QTimer(this);
        m_top_hide_timer->setInterval(180);
        QObject::connect(m_top_hide_timer, &QTimer::timeout, this, [this] {
            if (m_tabs == nullptr || currentIndex() != 0 || !m_tabs->expanded()) {
                m_top_hide_timer->stop();
                return;
            }
            const auto top_band = QRect(mapToGlobal(QPoint(edge_hover_width, 0)), QSize((std::max)(0, width() - edge_hover_width * 2), 30));
            if (!top_band.contains(QCursor::pos())) {
                m_tabs->conceal();
                m_top_hide_timer->stop();
            }
        });
        QObject::connect(this, &QTabWidget::currentChanged, this, [this] {
            update_auto_hide_state();
        });
    }

    ~WorkspaceTabWidget() override {
        if (auto* app = QApplication::instance(); app != nullptr) {
            app->removeEventFilter(this);
        }
    }

    void set_ribbon_locked(bool locked) {
        m_ribbon_locked = locked;
        update_auto_hide_state();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QTabWidget::resizeEvent(event);
        if (m_hover_band != nullptr) {
            m_hover_band->setGeometry(edge_hover_width, 0, (std::max)(0, width() - edge_hover_width * 2), 30);
            m_hover_band->setVisible(!m_ribbon_locked && currentIndex() == 0 && m_tabs != nullptr && !m_tabs->expanded());
            m_hover_band->raise();
        }
    }

    bool eventFilter(QObject* object, QEvent* event) override {
        if (
            currentIndex() == 0 &&
            !m_ribbon_locked &&
            m_tabs != nullptr &&
            !m_tabs->expanded() &&
            (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove)
        ) {
            const auto local_pos = mapFromGlobal(QCursor::pos());
            if (QRect(edge_hover_width, 0, (std::max)(0, width() - edge_hover_width * 2), 30).contains(local_pos)) {
                m_tabs->reveal();
                start_top_hide_watch();
            }
        }
        if (
            object == m_hover_band &&
            !m_ribbon_locked &&
            (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove)
        ) {
            if (m_tabs != nullptr) {
                m_tabs->reveal();
                start_top_hide_watch();
            }
        }
        return QTabWidget::eventFilter(object, event);
    }

private:
    void start_top_hide_watch() {
        if (m_top_hide_timer != nullptr && currentIndex() == 0) {
            m_top_hide_timer->start();
        }
    }

    void update_auto_hide_state() {
        if (m_tabs == nullptr || m_hover_band == nullptr) {
            return;
        }
        const auto browse_active = currentIndex() == 0;
        m_tabs->set_auto_hide_enabled(!m_ribbon_locked && browse_active);
        if (!m_ribbon_locked && browse_active) {
            if (top_trigger_contains_cursor()) {
                m_tabs->reveal();
                start_top_hide_watch();
            } else {
                m_tabs->conceal();
            }
        }
        m_hover_band->setVisible(!m_ribbon_locked && browse_active && !m_tabs->expanded());
        if (m_hover_band->isVisible()) {
            m_hover_band->raise();
        }
    }

    [[nodiscard]] bool top_trigger_contains_cursor() const {
        return QRect(
            mapToGlobal(QPoint(edge_hover_width, 0)),
            QSize((std::max)(0, width() - edge_hover_width * 2), 30)
        ).contains(QCursor::pos());
    }

    AutoHideTabBar* m_tabs = nullptr;
    QWidget* m_hover_band = nullptr;
    QTimer* m_top_hide_timer = nullptr;
    bool m_ribbon_locked = false;
    static constexpr int edge_hover_width = 30;
};

std::optional<uint64_t> resident_memory_bytes() {
#if defined(Q_OS_WIN) || defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            &counters,
            sizeof(counters)
        ) == 0) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(counters.WorkingSetSize);
#elif defined(Q_OS_LINUX) || defined(__linux__)
    QFile file(QStringLiteral("/proc/self/status"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const auto line = file.readLine();
            if (!line.startsWith("VmRSS:")) {
                continue;
            }
            const auto text = QString::fromLatin1(line).simplified();
            const auto parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() < 2) {
                break;
            }
            bool ok = false;
            const auto kib = parts[1].toULongLong(&ok);
            if (ok) {
                return static_cast<uint64_t>(kib) * 1024ull;
            }
            break;
        }
    }

    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto line = QString::fromLatin1(statm.readLine()).simplified();
        const auto parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool ok = false;
            const auto pages = parts[1].toULongLong(&ok);
            const auto page_size = sysconf(_SC_PAGESIZE);
            if (ok && page_size > 0) {
                return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
            }
        }
    }

    return std::nullopt;
#elif defined(Q_OS_MACOS) || defined(__APPLE__)
    mach_task_basic_info info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t result = task_info(
        mach_task_self(),
        MACH_TASK_BASIC_INFO,
        reinterpret_cast<task_info_t>(&info),
        &count
    );
    if (result != KERN_SUCCESS) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(info.resident_size);
#else
    return std::nullopt;
#endif
}

QString format_memory_size(uint64_t bytes) {
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = mib * 1024.0;
    const auto value = static_cast<double>(bytes);
    if (value >= gib) {
        return QCoreApplication::translate("MainWindow.Chrome", "%1 GB").arg(value / gib, 0, 'f', 2);
    }
    return QCoreApplication::translate("MainWindow.Chrome", "%1 MB").arg(value / mib, 0, 'f', value >= 100.0 * mib ? 0 : 1);
}



} // namespace

void MainWindow::bind_ui_text(
    QObject* object,
    const char* property,
    const char* source,
    const char* context
) {
    if (object != nullptr) {
        object->setProperty(property, QCoreApplication::translate(context, source));
        m_ui_text_bindings.push_back({object, property, source, context});
    }
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event == nullptr || event->type() != QEvent::LanguageChange || m_retranslate_pending) {
        return;
    }
    m_retranslate_pending = true;
    QTimer::singleShot(0, this, [this] {
        m_retranslate_pending = false;
        retranslate_ui();
    });
}

void MainWindow::sync_language_actions() {
    if (m_language_group == nullptr) {
        return;
    }
    const auto current = i18n::TranslationManager::instance().selected_code();
    for (auto* action : m_language_group->actions()) {
        const QSignalBlocker blocker(action);
        action->setChecked(action->data().toString() == current);
    }
}

void MainWindow::apply_pending_language() {
    if (m_pending_language_code.isEmpty()) {
        return;
    }
    if (has_background_work()) {
        sync_language_actions();
        statusBar()->showMessage(
            QCoreApplication::translate("MainWindow.Chrome", "Language change will apply after background work finishes."),
            3000
        );
        QTimer::singleShot(100, this, &MainWindow::apply_pending_language);
        return;
    }

    const auto code = std::exchange(m_pending_language_code, {});
    if (!i18n::TranslationManager::instance().set_language_code(code)) {
        statusBar()->showMessage(
            QCoreApplication::translate("MainWindow.Chrome", "Could not load the selected language."),
            5000
        );
    }
    sync_language_actions();
}

void MainWindow::retranslate_ui() {
    // Model/filter refreshes can temporarily remap current indexes. Keep those
    // internal updates from being interpreted as a new file or entry choice.
    const QSignalBlocker file_selection_blocker(m_file_view->selectionModel());
    const QSignalBlocker entry_selection_blocker(m_entry_view->selectionModel());
    const QSignalBlocker nested_selection_blocker(m_nested_entry_view->selectionModel());

    for (const auto& binding : m_ui_text_bindings) {
        if (binding.object != nullptr) {
            binding.object->setProperty(
                binding.property,
                QCoreApplication::translate(binding.context, binding.source)
            );
        }
    }

    setWindowTitle(app_title());
    sync_language_actions();
    if (m_file_sort != nullptr && m_file_sort->count() >= 4) {
        m_file_sort->setItemText(0, QCoreApplication::translate("MainWindow.Chrome", "Name A-Z"));
        m_file_sort->setItemText(1, QCoreApplication::translate("MainWindow.Chrome", "Name Z-A"));
        m_file_sort->setItemText(2, QCoreApplication::translate("MainWindow.Chrome", "Smallest"));
        m_file_sort->setItemText(3, QCoreApplication::translate("MainWindow.Chrome", "Largest"));
    }
    update_entry_view_mode_labels(m_acb_cue_sheet != nullptr);
    const bool playing = m_audio_player != nullptr &&
        m_audio_player->playbackState() == QMediaPlayer::PlayingState;
    m_media.play_button->setText(playing
        ? QCoreApplication::translate("MainWindow.Chrome", "Pause")
        : QCoreApplication::translate("MainWindow.Chrome", "Play"));
    if (m_audio_source_path.isEmpty()) {
        m_media.status_label->setText(QCoreApplication::translate("MainWindow.Chrome", "No playable audio selected"));
    }
    retranslate_decryption_keys_window();

    if (m_file_model != nullptr) {
        m_file_model->retranslate();
    }
    refresh_file_type_filter(m_file_type_filter, m_file_model);
    if (m_entry_model != nullptr) {
        m_entry_model->retranslate();
    }
    if (m_nested_entry_model != nullptr) {
        m_nested_entry_model->retranslate();
    }
    if (m_editor_workspace != nullptr) {
        m_editor_workspace->retranslate();
    }

    if (m_workspace_tabs != nullptr && m_workspace_tabs->count() >= 2) {
        m_workspace_tabs->setTabText(0, QCoreApplication::translate("MainWindow.Chrome", "Browse"));
        m_workspace_tabs->setTabText(1, QCoreApplication::translate("MainWindow.Chrome", "Editor"));
    }
    if (m_preview_tabs != nullptr && m_preview_tabs->count() >= 2) {
        m_preview_tabs->setTabText(0, QCoreApplication::translate("MainWindow.Chrome", "Preview"));
        m_preview_tabs->setTabText(1, QCoreApplication::translate("MainWindow.Chrome", "Raw"));
    }

    const LoadedDocument* current_document = nullptr;
    if (
        m_file_view != nullptr &&
        m_file_proxy != nullptr &&
        m_file_model != nullptr &&
        m_file_view->currentIndex().isValid()
    ) {
        current_document = m_file_model->document_at(
            m_file_proxy->mapToSource(m_file_view->currentIndex()).row()
        );
    }
    if (current_document != nullptr) {
        m_doc_title->setText(utf8_to_qstring(current_document->display_name));
        m_doc_subtitle->setText(utf8_to_qstring(localized_document_format(*current_document)));
        populate_info_grid(m_info_grid, current_document->info);
        update_document_key_panel(current_document);
        const bool has_entries = !current_document->entries.empty();
        const auto extract_text = has_entries
            ? QCoreApplication::translate("MainWindow.Chrome", "Extract All")
            : QCoreApplication::translate("MainWindow.Chrome", "Extract");
        const auto raw_text = has_entries
            ? QCoreApplication::translate("MainWindow.Chrome", "All Raw")
            : QCoreApplication::translate("MainWindow.Chrome", "Raw");
        m_doc_extract_button->setText(extract_text);
        m_doc_extract_raw_button->setText(raw_text);
    } else {
        const auto title = QCoreApplication::translate("MainWindow.Chrome", "No file selected");
        const auto subtitle = QCoreApplication::translate("MainWindow.Chrome", "Open or drop CRI files to inspect them.");
        m_doc_title->setText(title);
        m_doc_subtitle->setText(subtitle);
    }
    if (m_current_preview_entry) {
        auto title = utf8_to_qstring(m_current_preview_entry->name);
        if (title.startsWith(QLatin1String(mux_preview_prefix.data(), mux_preview_prefix.size()))) {
            title.remove(0, static_cast<int>(mux_preview_prefix.size()));
        }
        m_nested_title->setText(title);
        m_nested_subtitle->setText(utf8_to_qstring(m_current_preview_entry->type));
        populate_entry_preview_metadata(*m_current_preview_entry);
        update_preview_key_panel(*m_current_preview_entry);
    }

    update_file_sort();
    update_file_list_status();
    update_entry_selection_status();
    update_entry_path_bar();
    update_memory_usage_label();
}

void MainWindow::build_ui() {
    m_file_model = new FileListModel(this);
    auto* file_proxy = new FileFilterProxyModel(this);
    m_file_proxy = file_proxy;
    m_file_proxy->setSourceModel(m_file_model);
    m_file_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_file_proxy->sort(0);

    m_entry_model = new EntryTableModel(this);
    m_entry_proxy = new QSortFilterProxyModel(this);
    m_entry_proxy->setSourceModel(m_entry_model);
    m_entry_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_entry_proxy->setFilterKeyColumn(0);
    m_entry_proxy->setFilterRole(EntryTableModel::FullPathRole);
    m_entry_proxy->setSortRole(EntryTableModel::SortRole);
    m_entry_proxy->setRecursiveFilteringEnabled(true);

    m_nested_entry_model = new EntryTableModel(this);

    m_file_filter = new QLineEdit(this);
    m_file_filter->setObjectName(QStringLiteral("SearchField"));
    m_file_filter->setPlaceholderText(QCoreApplication::translate("MainWindow.Chrome", "Search files"));
    m_file_sort = new QComboBox(this);
    m_file_sort->setObjectName(QStringLiteral("FileSortCombo"));
    m_file_sort->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Sort loaded assets"));
    m_file_sort->addItem(QCoreApplication::translate("MainWindow.Chrome", "Name A-Z"), QVariant::fromValue<int>(0));
    m_file_sort->addItem(QCoreApplication::translate("MainWindow.Chrome", "Name Z-A"), QVariant::fromValue<int>(1));
    m_file_sort->addItem(QCoreApplication::translate("MainWindow.Chrome", "Smallest"), QVariant::fromValue<int>(2));
    m_file_sort->addItem(QCoreApplication::translate("MainWindow.Chrome", "Largest"), QVariant::fromValue<int>(3));
    m_file_type_filter = new FileTypeFilterCombo(this);
    m_file_type_filter->setObjectName(QStringLiteral("FileTypeFilterCombo"));
    m_file_type_filter->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Filter loaded files by detected type"));
    m_file_type_filter->view()->setMouseTracking(true);
    refresh_file_type_filter(m_file_type_filter, m_file_model);
    m_file_view = new QListView(this);
    m_file_view->setObjectName(QStringLiteral("LoadedFileView"));
    m_file_view->setModel(m_file_proxy);
    m_file_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_file_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_file_view->setUniformItemSizes(true);
    m_file_view->setAlternatingRowColors(false);
    m_file_view->setMouseTracking(true);
    m_file_view->setSpacing(2);
    m_file_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_file_view->setItemDelegate(new LoadedFileDelegate(m_file_filter, m_file_view));
    m_file_list_status = new QLabel(QCoreApplication::translate("MainWindow.Chrome", "0 loaded"), this);
    m_file_list_status->setObjectName(QStringLiteral("FileListStatus"));

    m_left_panel_button = make_panel_button(make_sidebar_icon(true), QCoreApplication::translate("MainWindow.Chrome", "Toggle loaded files panel"), this);
    m_left_panel_button->setChecked(true);
    connect(m_left_panel_button, &QToolButton::clicked, this, [this](bool checked) {
        if (m_toggle_left_action != nullptr) {
            m_toggle_left_action->setChecked(checked);
        }
        toggle_left_panel();
    });
    m_clear_files_button = make_rail_action_button(
        make_action_icon(ActionGlyph::Clear),
        QCoreApplication::translate("MainWindow.Chrome", "Unload all files"),
        this
    );
    connect(m_clear_files_button, &QToolButton::clicked, this, &MainWindow::clear_loaded_files);

    m_left_panel = new QWidget(this);
    m_left_panel->setObjectName(QStringLiteral("LoadedPanel"));
    auto* left_layout = new QVBoxLayout(m_left_panel);
    left_layout->setContentsMargins(8, 8, 8, 8);
    left_layout->setSpacing(6);
    auto* file_sort_row = new QWidget(m_left_panel);
    auto* file_sort_layout = new QHBoxLayout(file_sort_row);
    file_sort_layout->setContentsMargins(0, 0, 0, 0);
    file_sort_layout->setSpacing(6);
    file_sort_layout->addWidget(m_file_sort, 1);
    file_sort_layout->addWidget(m_file_type_filter, 1);
    left_layout->addWidget(m_file_filter);
    left_layout->addWidget(file_sort_row);
    left_layout->addWidget(m_file_view, 1);
    left_layout->addWidget(m_file_list_status);
    m_left_panel->setMinimumWidth(260);

    m_doc_title = new QLabel(QCoreApplication::translate("MainWindow.Chrome", "No file selected"), this);
    m_doc_title->setObjectName(QStringLiteral("DocumentTitle"));
    m_doc_subtitle = new QLabel(QCoreApplication::translate("MainWindow.Chrome", "Open or drop CRI files to inspect them."), this);
    m_doc_subtitle->setObjectName(QStringLiteral("DocumentSubtitle"));

    auto* info_content = new QWidget(this);
    m_info_grid = new QGridLayout(info_content);
    m_info_grid->setContentsMargins(0, 0, 0, 0);
    m_info_grid->setHorizontalSpacing(18);
    m_info_grid->setVerticalSpacing(2);
    info_content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_doc_key_panel = make_key_panel(m_doc_key_label, m_doc_key_input, m_doc_key_base_input, m_doc_key_apply, this);
    m_doc_mux_preview_button = new QToolButton(this);
    m_doc_mux_preview_button->setObjectName(QStringLiteral("ActionButton"));
    m_doc_mux_preview_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Mux preview"));
    m_doc_mux_preview_button->setIcon(make_action_icon(ActionGlyph::MuxPreview));
    m_doc_mux_preview_button->setIconSize(QSize(18, 18));
    m_doc_mux_preview_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_doc_mux_preview_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Return to the composed USM/SFD preview"));
    m_doc_mux_preview_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Show mux preview"));
    m_doc_mux_preview_button->hide();
    m_doc_extract_button = new QToolButton(this);
    m_doc_extract_button->setObjectName(QStringLiteral("ActionButton"));
    m_doc_extract_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Extract All"));
    m_doc_extract_button->setIcon(make_action_icon(ActionGlyph::Extract));
    m_doc_extract_button->setIconSize(QSize(18, 18));
    m_doc_extract_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_doc_extract_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Extract the selected loaded file"));
    m_doc_extract_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Extract selected loaded file"));
    m_doc_extract_button->hide();
    m_doc_extract_raw_button = new QToolButton(this);
    m_doc_extract_raw_button->setObjectName(QStringLiteral("ActionButton"));
    m_doc_extract_raw_button->setText(QCoreApplication::translate("MainWindow.Chrome", "All Raw"));
    m_doc_extract_raw_button->setIcon(make_action_icon(ActionGlyph::RawExtract));
    m_doc_extract_raw_button->setIconSize(QSize(18, 18));
    m_doc_extract_raw_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_doc_extract_raw_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Extract the selected loaded file without decode or mux conversion"));
    m_doc_extract_raw_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Raw extract selected loaded file"));
    m_doc_extract_raw_button->hide();

    m_entry_filter = new QLineEdit(this);
    m_entry_filter->setObjectName(QStringLiteral("SearchField"));
    m_entry_filter->setPlaceholderText(QCoreApplication::translate("MainWindow.Chrome", "Search entries"));
    m_entry_view_mode = new QComboBox(this);
    m_entry_view_mode->setObjectName(QStringLiteral("EntryViewModeCombo"));
    m_entry_view_mode->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Choose archive entry view mode"));
    m_entry_view_mode->addItem(QCoreApplication::translate("MainWindow.Chrome", "Tree"), QVariant::fromValue<int>(0));
    m_entry_view_mode->addItem(QCoreApplication::translate("MainWindow.Chrome", "List"), QVariant::fromValue<int>(1));
    m_entry_view_mode->setMinimumWidth(116);
    m_entry_view_mode->hide();
    auto* entry_view_selector = new QWidget(this);
    entry_view_selector->setObjectName(QStringLiteral("EntryViewModeSelector"));
    auto* entry_view_selector_layout = new QHBoxLayout(entry_view_selector);
    entry_view_selector_layout->setContentsMargins(0, 0, 0, 0);
    entry_view_selector_layout->setSpacing(0);
    entry_view_selector_layout->addWidget(m_entry_view_mode, 0);
    for (const auto& item : {std::pair{QCoreApplication::translate("MainWindow.Chrome", "Tree"), 0}, std::pair{QCoreApplication::translate("MainWindow.Chrome", "List"), 1}}) {
        auto* button = new QToolButton(entry_view_selector);
        button->setObjectName(QStringLiteral("EntryViewModeSegment"));
        button->setText(item.first);
        button->setProperty("modeValue", item.second);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Show entries as %1").arg(item.first.toLower()));
        entry_view_selector_layout->addWidget(button, 0);
        connect(button, &QToolButton::clicked, m_entry_view_mode, [this, value = item.second] {
            m_entry_view_mode->setCurrentIndex(value);
        });
    }
    sync_segment_buttons(entry_view_selector, 0);
    m_entry_view = new QTreeView(this);
    m_entry_view->setObjectName(QStringLiteral("EntryTree"));
    m_entry_view->setModel(m_entry_proxy);
    m_entry_view->setAlternatingRowColors(false);
    m_entry_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_entry_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_entry_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_entry_view->setUniformRowHeights(true);
    m_entry_view->setAnimated(false);
    m_entry_view->setMouseTracking(true);
    m_entry_view->setRootIsDecorated(true);
    m_entry_view->setItemDelegate(new EntryTreeDelegate(m_entry_filter, m_file_filter, m_entry_view));
    m_entry_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_entry_view->setSortingEnabled(true);
    m_entry_view->installEventFilter(this);
    m_entry_view->sortByColumn(0, Qt::AscendingOrder);
    m_entry_view->header()->setSectionsClickable(true);
    m_entry_view->header()->setSortIndicatorShown(true);
    m_entry_view->header()->setStretchLastSection(false);
    m_entry_view->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_entry_view->header()->setMinimumSectionSize(64);
    m_entry_view->header()->resizeSection(0, 340);
    m_entry_view->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_entry_view->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_entry_view->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_entry_view->header()->setSectionResizeMode(4, QHeaderView::Interactive);
    m_entry_view->setColumnWidth(1, 120);
    m_entry_view->setColumnWidth(2, 120);
    m_entry_view->setColumnWidth(3, 100);
    m_entry_view->setColumnWidth(4, 220);
    m_entry_filter->hide();
    m_entry_view->hide();
    m_entry_selection_status = new QLabel(QCoreApplication::translate("MainWindow.Chrome", "0 selected"), this);
    m_entry_selection_status->setObjectName(QStringLiteral("EntrySelectionStatus"));
    m_entry_selection_status->hide();

    auto* right = new QWidget(this);
    right->setObjectName(QStringLiteral("DocumentPanel"));
    auto* right_layout = new QVBoxLayout(right);
    right_layout->setContentsMargins(8, 8, 8, 8);
    right_layout->setSpacing(6);
    right_layout->addWidget(m_doc_title);
    right_layout->addWidget(m_doc_subtitle);
    right_layout->addWidget(info_content);
    auto* doc_actions = new QHBoxLayout();
    doc_actions->setContentsMargins(0, 0, 0, 0);
    doc_actions->setSpacing(8);
    doc_actions->addWidget(m_doc_extract_button, 0);
    doc_actions->addWidget(m_doc_extract_raw_button, 0);
    doc_actions->addStretch(1);
    doc_actions->addWidget(m_doc_mux_preview_button, 0);
    right_layout->addLayout(doc_actions);
    right_layout->addWidget(m_doc_key_panel);
    m_entry_filter_row = new QWidget(this);
    auto* entry_filter_layout = new QHBoxLayout(m_entry_filter_row);
    entry_filter_layout->setContentsMargins(0, 0, 0, 0);
    entry_filter_layout->setSpacing(8);
    entry_filter_layout->addWidget(m_entry_filter, 1);
    entry_filter_layout->addWidget(entry_view_selector, 0);
    right_layout->addWidget(m_entry_filter_row);
    m_entry_filter_row->hide();
    m_entry_path_row = new QWidget(this);
    auto* entry_path_layout = new QHBoxLayout(m_entry_path_row);
    entry_path_layout->setContentsMargins(0, 0, 0, 0);
    entry_path_layout->setSpacing(6);
    m_entry_up_button = new QToolButton(m_entry_path_row);
    m_entry_up_button->setObjectName(QStringLiteral("SmallToolButton"));
    m_entry_up_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Up"));
    m_entry_up_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Go to the parent archive folder"));
    m_entry_up_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Go to parent archive folder"));
    m_entry_path_label = new QLabel(m_entry_path_row);
    m_entry_path_label->setObjectName(QStringLiteral("EntryPathLabel"));
    m_entry_path_label->setTextFormat(Qt::RichText);
    m_entry_path_label->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    m_entry_up_button->hide();
    entry_path_layout->addWidget(m_entry_up_button, 0);
    entry_path_layout->addWidget(m_entry_path_label, 1);
    right_layout->addWidget(m_entry_path_row);
    m_entry_path_row->hide();
    right_layout->addWidget(m_entry_view, 1);
    right_layout->addWidget(m_entry_selection_status);
    m_main_bottom_spacer = new QWidget(right);
    m_main_bottom_spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    right_layout->addWidget(m_main_bottom_spacer, 1);

    m_nested_panel = new QWidget(this);
    m_nested_panel->setObjectName(QStringLiteral("PreviewPanel"));
    m_nested_panel->setMinimumWidth(0);
    auto* nested_layout = new QVBoxLayout(m_nested_panel);
    nested_layout->setContentsMargins(8, 8, 8, 8);
    nested_layout->setSpacing(8);
    m_preview_panel_button = make_panel_button(make_sidebar_icon(false), QCoreApplication::translate("MainWindow.Chrome", "Toggle entry preview panel"), m_nested_panel);
    m_preview_panel_button->setChecked(false);
    connect(m_preview_panel_button, &QToolButton::clicked, this, [this](bool checked) {
        if (m_toggle_preview_action != nullptr) {
            m_toggle_preview_action->setChecked(checked);
        }
        toggle_preview_panel();
    });
    m_nested_title = new QLabel(QCoreApplication::translate("MainWindow.Chrome", "Entry preview"), m_nested_panel);
    m_nested_title->setObjectName(QStringLiteral("PreviewTitle"));
    m_nested_subtitle = new QLabel(QCoreApplication::translate("MainWindow.Chrome", "Select a supported embedded file."), m_nested_panel);
    m_nested_subtitle->setObjectName(QStringLiteral("PreviewSubtitle"));
    m_preview_extract_button = new QToolButton(m_nested_panel);
    m_preview_extract_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_extract_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Extract Entry"));
    m_preview_extract_button->setIcon(make_action_icon(ActionGlyph::Extract));
    m_preview_extract_button->setIconSize(QSize(18, 18));
    m_preview_extract_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_extract_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Extract this previewed archive entry"));
    m_preview_extract_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Extract previewed archive entry"));
    m_preview_extract_button->hide();
    m_preview_extract_raw_button = new QToolButton(m_nested_panel);
    m_preview_extract_raw_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_extract_raw_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Entry Raw"));
    m_preview_extract_raw_button->setIcon(make_action_icon(ActionGlyph::RawExtract));
    m_preview_extract_raw_button->setIconSize(QSize(18, 18));
    m_preview_extract_raw_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_extract_raw_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Extract this previewed archive entry without decode or mux conversion"));
    m_preview_extract_raw_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Raw extract previewed archive entry"));
    m_preview_extract_raw_button->hide();
    m_preview_recover_key_button = new QToolButton(m_nested_panel);
    m_preview_recover_key_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_recover_key_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Recover HCA Key"));
    m_preview_recover_key_button->setIcon(make_action_icon(ActionGlyph::RecoverKey));
    m_preview_recover_key_button->setIconSize(QSize(18, 18));
    m_preview_recover_key_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_recover_key_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Recover an HCA type-56 key from this previewed file"));
    m_preview_recover_key_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Recover HCA key from previewed file"));
    m_preview_recover_key_button->hide();
    m_preview_recover_usm_key_button = new QToolButton(m_nested_panel);
    m_preview_recover_usm_key_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_recover_usm_key_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Recover USM Key"));
    m_preview_recover_usm_key_button->setIcon(make_action_icon(ActionGlyph::RecoverKey));
    m_preview_recover_usm_key_button->setIconSize(QSize(18, 18));
    m_preview_recover_usm_key_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_recover_usm_key_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Recover a USM mask key from audio and video evidence"));
    m_preview_recover_usm_key_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Recover USM key from previewed file"));
    m_preview_recover_usm_key_button->hide();
    m_preview_recover_adx_key_button = new QToolButton(m_nested_panel);
    m_preview_recover_adx_key_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_recover_adx_key_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Recover ADX Key"));
    m_preview_recover_adx_key_button->setIcon(make_action_icon(ActionGlyph::RecoverKey));
    m_preview_recover_adx_key_button->setIconSize(QSize(18, 18));
    m_preview_recover_adx_key_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_recover_adx_key_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Recover ADX or AHX key from previewed file"));
    m_preview_recover_adx_key_button->hide();
    m_preview_recover_aac_key_button = new QToolButton(m_nested_panel);
    m_preview_recover_aac_key_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_recover_aac_key_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Recover AAC Key"));
    m_preview_recover_aac_key_button->setIcon(make_action_icon(ActionGlyph::RecoverKey));
    m_preview_recover_aac_key_button->setIconSize(QSize(18, 18));
    m_preview_recover_aac_key_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_recover_aac_key_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Recover the effective AAC key from this ACB/AWB M4A source"));
    m_preview_recover_aac_key_button->setAccessibleName(QCoreApplication::translate("MainWindow.Chrome", "Recover AAC key from previewed ACB or AWB source"));
    m_preview_recover_aac_key_button->hide();
    auto* nested_content = new QWidget(m_nested_panel);
    m_nested_info_panel = nested_content;
    m_nested_info_grid = new QGridLayout(nested_content);
    m_nested_info_grid->setContentsMargins(0, 0, 0, 0);
    m_nested_info_grid->setHorizontalSpacing(16);
    m_nested_info_grid->setVerticalSpacing(5);
    m_nested_info_panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_acb_cue_controls = new QWidget(m_nested_panel);
    m_acb_cue_controls->setObjectName(QStringLiteral("AcbCueControls"));
    auto* cue_controls_layout = new QVBoxLayout(m_acb_cue_controls);
    cue_controls_layout->setContentsMargins(0, 0, 0, 0);
    cue_controls_layout->setSpacing(6);
    m_acb_cue_selector_form = new QFormLayout();
    m_acb_cue_selector_form->setContentsMargins(0, 0, 0, 0);
    m_acb_cue_selector_form->setHorizontalSpacing(12);
    m_acb_cue_selector_form->setVerticalSpacing(5);
    cue_controls_layout->addLayout(m_acb_cue_selector_form);
    m_acb_cue_route_combo = new QComboBox(m_acb_cue_controls);
    m_acb_cue_route_combo->setObjectName(QStringLiteral("AcbCueRouteCombo"));
    m_acb_cue_route_combo->setToolTip(QCoreApplication::translate(
        "MainWindow.Chrome",
        "Choose a statically renderable cue path"));
    m_acb_cue_selector_form->addRow(
        QCoreApplication::translate("MainWindow.Chrome", "Playback path"),
        m_acb_cue_route_combo);
    m_acb_include_empty_holds = new QCheckBox(
        QCoreApplication::translate(
            "MainWindow.Chrome",
            "Include silent hold blocks"),
        m_acb_cue_controls);
    m_acb_include_empty_holds->setObjectName(
        QStringLiteral("AcbIncludeEmptyHolds"));
    m_acb_include_empty_holds->setToolTip(QCoreApplication::translate(
        "MainWindow.Chrome",
        "Render authored empty infinite holds as one finite silence block"));
    m_acb_include_empty_holds->setAccessibleName(
        QCoreApplication::translate(
            "MainWindow.Chrome",
            "Include silent ACB hold blocks in cue preview"));
    cue_controls_layout->addWidget(m_acb_include_empty_holds);
    m_acb_cue_controls->hide();
    m_preview_key_panel = make_key_panel(m_preview_key_label, m_preview_key_input, m_preview_key_base_input, m_preview_key_apply, m_nested_panel);
    m_preview_tabs = new QTabWidget(m_nested_panel);
    m_preview_tabs->setObjectName(QStringLiteral("PreviewTabs"));
    m_preview_tabs->setDocumentMode(false);
    m_preview_tabs->hide();
    auto* preview_tab = new QWidget(m_preview_tabs);
    auto* preview_tab_layout = new QVBoxLayout(preview_tab);
    preview_tab_layout->setContentsMargins(0, 0, 0, 0);
    preview_tab_layout->setSpacing(8);
    m_nested_image = new QLabel(m_nested_panel);
    m_nested_image->setAlignment(Qt::AlignCenter);
    m_nested_image->setBackgroundRole(QPalette::Base);
    m_nested_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_nested_image_scroll = new QScrollArea(m_nested_panel);
    m_nested_image_scroll->setWidget(m_nested_image);
    m_nested_image_scroll->setWidgetResizable(true);
    m_nested_image_scroll->hide();
    m_nested_entry_view = new QTreeView(m_nested_panel);
    m_nested_entry_view->setObjectName(QStringLiteral("NestedEntryTree"));
    m_nested_entry_view->setModel(m_nested_entry_model);
    m_nested_entry_view->setAlternatingRowColors(false);
    m_nested_entry_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nested_entry_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_nested_entry_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_nested_entry_view->setUniformRowHeights(true);
    m_nested_entry_view->setAnimated(false);
    m_nested_entry_view->setMouseTracking(true);
    m_nested_entry_view->setRootIsDecorated(true);
    m_nested_entry_view->setItemDelegate(new EntryTreeDelegate(m_entry_filter, m_file_filter, m_nested_entry_view));
    m_nested_entry_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nested_entry_view->header()->setStretchLastSection(false);
    m_nested_entry_view->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_nested_entry_view->header()->setMinimumSectionSize(64);
    m_nested_entry_view->header()->resizeSection(0, 240);
    m_nested_entry_view->setColumnWidth(1, 110);
    m_nested_entry_view->setColumnWidth(2, 110);
    m_nested_entry_view->setColumnWidth(3, 90);
    m_nested_entry_view->setColumnWidth(4, 180);
    m_nested_entry_view->hide();
    m_nested_body = new QPlainTextEdit(m_nested_panel);
    m_nested_body->setReadOnly(true);
    m_nested_body->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_nested_body->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_nested_body->setMinimumWidth(0);
    m_video = make_video_display(m_nested_panel);
    m_media = make_media_controls(m_nested_panel);
    m_media.play_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Play"));
    m_media.status_label->setText(QCoreApplication::translate("MainWindow.Chrome", "No playable audio selected"));
    m_media.panel->hide();
    m_raw_hex = new HexPreviewWidget(m_preview_tabs);
    preview_tab_layout->addWidget(m_video.frame, 8);
    preview_tab_layout->addWidget(m_media.panel);
    preview_tab_layout->addWidget(m_nested_entry_view, 8);
    preview_tab_layout->addWidget(m_nested_image_scroll, 8);
    preview_tab_layout->addWidget(m_nested_body, 8);
    preview_tab_layout->addStretch(1);
    m_preview_tabs->addTab(preview_tab, QCoreApplication::translate("MainWindow.Chrome", "Preview"));
    m_preview_tabs->addTab(m_raw_hex, QCoreApplication::translate("MainWindow.Chrome", "Raw"));
    m_preview_tabs->setTabEnabled(1, false);
    nested_layout->addWidget(m_nested_title);
    nested_layout->addWidget(m_nested_subtitle);
    auto* preview_actions = new QHBoxLayout();
    preview_actions->setContentsMargins(0, 0, 0, 0);
    preview_actions->setSpacing(8);
    preview_actions->addWidget(m_preview_extract_button, 0);
    preview_actions->addWidget(m_preview_extract_raw_button, 0);
    preview_actions->addWidget(m_preview_recover_key_button, 0);
    preview_actions->addWidget(m_preview_recover_usm_key_button, 0);
    preview_actions->addWidget(m_preview_recover_adx_key_button, 0);
    preview_actions->addWidget(m_preview_recover_aac_key_button, 0);
    preview_actions->addStretch(1);
    nested_layout->addLayout(preview_actions);
    nested_layout->addWidget(m_nested_info_panel);
    nested_layout->addWidget(m_acb_cue_controls);
    nested_layout->addWidget(m_preview_key_panel);
    nested_layout->addWidget(m_preview_tabs, 8);
    nested_layout->addStretch(1);
    m_video.frame->hide();
    m_video.widget->hide();
    m_nested_entry_view->hide();
    m_nested_body->hide();
    m_nested_panel->setMaximumWidth(0);
    m_nested_panel->hide();

    auto* content_host = new ContentHostWidget(this);
    m_content_host = content_host;
    m_content_host->setObjectName(QStringLiteral("AppChrome"));
    auto* host_layout = new QHBoxLayout(m_content_host);
    host_layout->setContentsMargins(0, 0, 0, 0);
    host_layout->setSpacing(0);
    m_left_edge_rail = new AutoHideRail(m_content_host);
    m_left_edge_rail->setObjectName(QStringLiteral("SideRail"));
    auto* left_rail_layout = new QVBoxLayout(m_left_edge_rail);
    left_rail_layout->setContentsMargins(4, 8, 4, 0);
    left_rail_layout->setSpacing(6);
    left_rail_layout->addWidget(m_left_panel_button, 0, Qt::AlignHCenter | Qt::AlignTop);
    left_rail_layout->addWidget(m_clear_files_button, 0, Qt::AlignHCenter | Qt::AlignTop);
    left_rail_layout->addStretch(1);
    auto* left_hover_band = new EdgeHoverBand([this] {
        if (m_left_edge_rail != nullptr) {
            auto* rail = static_cast<AutoHideRail*>(m_left_edge_rail);
            rail->reveal();
        }
    }, m_content_host);
    if (m_left_edge_rail != nullptr) {
        auto* rail = static_cast<AutoHideRail*>(m_left_edge_rail);
        rail->set_expanded_changed([left_hover_band](bool expanded) {
            left_hover_band->setVisible(!expanded);
            if (!expanded) {
                left_hover_band->raise();
            }
        });
    }
    m_splitter = new ColumnSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(15);
    m_splitter->addWidget(m_left_panel);
    m_splitter->addWidget(right);
    m_splitter->addWidget(m_nested_panel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        if (m_toggle_left_action != nullptr && m_toggle_left_action->isChecked()) {
            const auto sizes = m_splitter->sizes();
            if (!sizes.empty() && sizes[0] >= 260) {
                m_left_panel_width = sizes[0];
            }
        }
        if (m_toggle_preview_action != nullptr && m_toggle_preview_action->isChecked()) {
            const auto sizes = m_splitter->sizes();
            if (sizes.size() >= 3 && sizes[2] >= 320) {
                m_preview_panel_width = sizes[2];
            }
        }
        schedule_position_edge_buttons();
    });
    m_right_edge_rail = new AutoHideRail(m_content_host);
    m_right_edge_rail->setObjectName(QStringLiteral("PreviewRail"));
    auto* right_rail_layout = new QVBoxLayout(m_right_edge_rail);
    right_rail_layout->setContentsMargins(4, 8, 4, 0);
    right_rail_layout->setSpacing(0);
    right_rail_layout->addWidget(m_preview_panel_button, 0, Qt::AlignHCenter | Qt::AlignTop);
    right_rail_layout->addStretch(1);
    auto* right_hover_band = new EdgeHoverBand([this] {
        if (m_right_edge_rail != nullptr) {
            auto* rail = static_cast<AutoHideRail*>(m_right_edge_rail);
            rail->reveal();
        }
    }, m_content_host);
    if (m_right_edge_rail != nullptr) {
        auto* rail = static_cast<AutoHideRail*>(m_right_edge_rail);
        rail->set_hover_guard(m_nested_panel);
        rail->set_expanded_changed([right_hover_band](bool expanded) {
            right_hover_band->setVisible(!expanded);
            if (!expanded) {
                right_hover_band->raise();
            }
        });
    }
    host_layout->addWidget(m_left_edge_rail);
    host_layout->addWidget(m_splitter, 1);
    host_layout->addWidget(m_right_edge_rail);
    content_host->set_edge_hover_bands(left_hover_band, right_hover_band);
    m_splitter->setSizes({320, 1120, 0});

    m_editor_workspace = new EditorWorkspace(this);
    m_workspace_tabs = new WorkspaceTabWidget(this);
    m_workspace_tabs->setObjectName(QStringLiteral("WorkspaceTabs"));
    m_workspace_tabs->setDocumentMode(false);
    m_workspace_tabs->addTab(m_content_host, QCoreApplication::translate("MainWindow.Chrome", "Browse"));
    m_workspace_tabs->addTab(m_editor_workspace, QCoreApplication::translate("MainWindow.Chrome", "Editor"));
    connect(m_workspace_tabs, &QTabWidget::currentChanged, this, [this, content_host = m_content_host](int index) {
        if (m_workspace_tabs->widget(index) != content_host) {
            reset_audio_preview();
        }
    });
    setCentralWidget(m_workspace_tabs);

    m_drop_overlay = new QFrame(this);
    m_drop_overlay->setObjectName(QStringLiteral("DropOverlay"));
    m_drop_overlay->hide();
    m_drop_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* drop_layout = new QVBoxLayout(m_drop_overlay);
    drop_layout->setContentsMargins(28, 28, 28, 28);
    auto* drop_label = new QLabel(QCoreApplication::translate("MainWindow.Chrome", "Drop files or folders"), m_drop_overlay);
    drop_label->setObjectName(QStringLiteral("DropOverlayLabel"));
    drop_label->setAlignment(Qt::AlignCenter);
    drop_layout->addWidget(drop_label, 1);

    const std::array<QWidget*, 10> drop_targets = {
        m_workspace_tabs,
        m_content_host,
        m_left_panel,
        m_file_view,
        m_file_view->viewport(),
        m_entry_view,
        m_entry_view->viewport(),
        m_nested_panel,
        m_nested_entry_view,
        m_nested_entry_view->viewport()
    };
    for (auto* target : drop_targets) {
        target->setAcceptDrops(true);
        target->installEventFilter(this);
    }
    schedule_position_edge_buttons();

    m_loading_bar = new QProgressBar(this);
    m_loading_bar->setRange(0, 0);
    m_loading_bar->setMaximumWidth(180);
    m_loading_bar->hide();
    m_loading_status_label = new QLabel(this);
    m_loading_status_label->setObjectName(QStringLiteral("LoadingStatus"));
    m_loading_status_label->setMinimumWidth(220);
    m_loading_status_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_loading_status_label->hide();
    m_cancel_extraction_button = new QToolButton(this);
    m_cancel_extraction_button->setText(QCoreApplication::translate("MainWindow.Chrome", "Cancel"));
    m_cancel_extraction_button->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancel_extraction_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_cancel_extraction_button->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Stop the current extraction"));
    m_cancel_extraction_button->hide();
    connect(m_cancel_extraction_button, &QToolButton::clicked, this, &MainWindow::cancel_extraction);
    statusBar()->addPermanentWidget(m_loading_status_label);
    statusBar()->addPermanentWidget(m_loading_bar);
    statusBar()->addPermanentWidget(m_cancel_extraction_button);

    m_work_timer = new QTimer(this);
    connect(m_work_timer, &QTimer::timeout, this, &MainWindow::poll_background_work);
    m_load_watcher = new QFutureWatcher<LoadResult>(this);
    connect(m_load_watcher, &QFutureWatcher<LoadResult>::finished, this, &MainWindow::consume_load_result);
    m_extract_watcher = new QFutureWatcher<ExtractionReport>(this);
    connect(m_extract_watcher, &QFutureWatcher<ExtractionReport>::finished, this, &MainWindow::consume_extract_result);
    m_materialize_watcher = new QFutureWatcher<MaterializeResult>(this);
    connect(m_materialize_watcher, &QFutureWatcher<MaterializeResult>::finished, this, &MainWindow::consume_materialize_result);
    m_preview_watcher = new QFutureWatcher<PreviewResult>(this);
    connect(m_preview_watcher, &QFutureWatcher<PreviewResult>::finished, this, &MainWindow::consume_preview_result);
    m_hca_key_recovery_watcher = new QFutureWatcher<HcaKeyRecoveryTaskResult>(this);
    connect(
        m_hca_key_recovery_watcher,
        &QFutureWatcher<HcaKeyRecoveryTaskResult>::finished,
        this,
        &MainWindow::consume_hca_key_recovery_result);
    m_usm_key_recovery_watcher = new QFutureWatcher<UsmKeyRecoveryTaskResult>(this);
    connect(
        m_usm_key_recovery_watcher,
        &QFutureWatcher<UsmKeyRecoveryTaskResult>::finished,
        this,
        &MainWindow::consume_usm_key_recovery_result);
    m_adx_key_recovery_watcher = new QFutureWatcher<AdxKeyRecoveryTaskResult>(this);
    connect(
        m_adx_key_recovery_watcher,
        &QFutureWatcher<AdxKeyRecoveryTaskResult>::finished,
        this,
        &MainWindow::consume_adx_key_recovery_result);
    m_aac_key_recovery_watcher = new QFutureWatcher<AacKeyRecoveryTaskResult>(this);
    connect(
        m_aac_key_recovery_watcher,
        &QFutureWatcher<AacKeyRecoveryTaskResult>::finished,
        this,
        &MainWindow::consume_aac_key_recovery_result);
    connect(m_file_filter, &QLineEdit::textChanged, file_proxy, [this, file_proxy](const QString& text) {
        file_proxy->set_search_text(text);
        update_file_list_status();
    });
    connect(m_file_filter, &QLineEdit::textChanged, this, &MainWindow::update_file_list_status);
    connect(m_file_filter, &QLineEdit::textChanged, m_file_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_file_filter, &QLineEdit::textChanged, m_entry_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_file_filter, &QLineEdit::textChanged, m_nested_entry_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_file_sort, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        update_file_sort();
        update_file_list_status();
    });
    static_cast<FileTypeFilterCombo*>(m_file_type_filter)->set_selection_changed([this, file_proxy] {
        update_file_type_filter_label(m_file_type_filter);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
        update_file_list_status();
    });
    connect(m_file_view, &QWidget::customContextMenuRequested, this, &MainWindow::show_loaded_file_context_menu);
    connect(m_entry_view, &QWidget::customContextMenuRequested, this, &MainWindow::show_entry_context_menu);
    connect(m_nested_entry_view, &QWidget::customContextMenuRequested, this, &MainWindow::show_nested_entry_context_menu);
    connect(m_doc_extract_button, &QToolButton::clicked, this, [this] {
        auto targets = selected_file_targets();
        auto mux_audio_choice = targets.size() == 1 ? current_mux_audio_choice() : std::nullopt;
        start_extraction(
            std::move(targets),
            ExtractionMode::Decoded,
            mux_audio_choice
        );
    });
    connect(m_doc_extract_raw_button, &QToolButton::clicked, this, [this] {
        start_extraction(selected_file_targets(), ExtractionMode::Raw);
    });
    connect(m_preview_extract_button, &QToolButton::clicked, this, [this] {
        start_extraction(current_preview_entry_targets(), ExtractionMode::Decoded);
    });
    connect(m_preview_extract_raw_button, &QToolButton::clicked, this, [this] {
        start_extraction(current_preview_entry_targets(), ExtractionMode::Raw);
    });
    connect(m_preview_recover_key_button, &QToolButton::clicked, this, [this] {
        auto sources = current_preview_recovery_sources();
        const auto label = m_current_preview_entry.has_value()
            ? utf8_to_qstring(m_current_preview_entry->name)
            : QCoreApplication::translate("MainWindow.Chrome", "Previewed file");
        start_hca_key_recovery(std::move(sources), label);
    });
    connect(m_preview_recover_usm_key_button, &QToolButton::clicked, this, [this] {
        auto sources = current_preview_usm_recovery_sources();
        const auto label = m_current_preview_entry.has_value()
            ? utf8_to_qstring(m_current_preview_entry->name)
            : QCoreApplication::translate("MainWindow.Chrome", "Previewed file");
        start_usm_key_recovery(std::move(sources), label);
    });
    connect(m_preview_recover_adx_key_button, &QToolButton::clicked, this, [this] {
        const auto kind = current_preview_adx_recovery_kind();
        if (!kind) {
            return;
        }
        auto sources = current_preview_adx_recovery_sources();
        const auto label = m_current_preview_entry.has_value()
            ? utf8_to_qstring(m_current_preview_entry->name)
            : QCoreApplication::translate("MainWindow.Chrome", "Previewed file");
        start_adx_key_recovery(std::move(sources), *kind, label);
    });
    connect(m_preview_recover_aac_key_button, &QToolButton::clicked, this, [this] {
        auto sources = current_preview_aac_recovery_sources();
        const auto label = m_current_preview_entry.has_value()
            ? utf8_to_qstring(m_current_preview_entry->name)
            : QCoreApplication::translate("MainWindow.Chrome", "Previewed ACB/AWB");
        start_aac_key_recovery(std::move(sources), label);
    });
    connect(m_preview_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 0 && !m_current_preview_entry.has_value() && !preview_running() &&
            m_file_view != nullptr && m_file_proxy != nullptr && m_file_model != nullptr &&
            m_file_view->currentIndex().isValid()) {
            const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
            if (const auto* document = ensure_loaded_document(source.row()); document != nullptr && is_mux_document(*document)) {
                start_document_mux_preview(*document, 0);
            }
            return;
        }
        if (index != 1 || m_current_preview_entry.has_value() || preview_running() ||
            m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr ||
            !m_file_view->currentIndex().isValid()) {
            return;
        }
        const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        if (const auto* document = ensure_loaded_document(source.row()); document != nullptr) {
            populate_document_raw_tab(*document, true);
        }
    });
    connect(m_file_proxy, &QAbstractItemModel::rowsInserted, this, &MainWindow::update_file_list_status);
    connect(m_file_proxy, &QAbstractItemModel::rowsRemoved, this, &MainWindow::update_file_list_status);
    connect(m_file_proxy, &QAbstractItemModel::modelReset, this, &MainWindow::update_file_list_status);
    connect(m_file_proxy, &QAbstractItemModel::layoutChanged, this, &MainWindow::update_file_list_status);
    connect(m_file_model, &QAbstractItemModel::rowsInserted, this, [this, file_proxy] {
        refresh_file_type_filter(m_file_type_filter, m_file_model);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
    });
    connect(m_file_model, &QAbstractItemModel::rowsRemoved, this, [this, file_proxy] {
        refresh_file_type_filter(m_file_type_filter, m_file_model);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
    });
    connect(m_file_model, &QAbstractItemModel::modelReset, this, [this, file_proxy] {
        refresh_file_type_filter(m_file_type_filter, m_file_model);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
    });
    connect(m_file_model, &QAbstractItemModel::dataChanged, this, [this, file_proxy] {
        refresh_file_type_filter(m_file_type_filter, m_file_model);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
    });
    connect(m_entry_filter, &QLineEdit::textChanged, m_entry_proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(m_entry_filter, &QLineEdit::textChanged, m_entry_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_entry_filter, &QLineEdit::textChanged, m_nested_entry_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_entry_view, &QTreeView::doubleClicked, this, [this](const QModelIndex&) {
        activate_current_entry();
    });
    connect(m_entry_up_button, &QToolButton::clicked, this, [this] {
        if (m_entry_model != nullptr && m_entry_model->flat_can_go_up()) {
            set_entry_list_path(QString::fromStdString(m_entry_model->flat_parent_path()));
        }
    });
    connect(m_entry_path_label, &QLabel::linkActivated, this, [this](const QString& path) {
        set_entry_list_path(path == QStringLiteral("@root") ? QString{} : path);
    });
    connect(m_entry_view_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        sync_segment_buttons(m_entry_view_mode->parentWidget(), index);
        if (m_entry_model == nullptr || m_entry_view == nullptr) {
            return;
        }
        if (m_acb_cue_sheet != nullptr) {
            apply_current_entry_view_mode();
            return;
        }
        const auto flat = index == 1;
        if (flat && m_entry_model->has_custom_columns()) {
            return;
        }
        m_entry_model->set_flat_mode(flat);
        m_entry_view->setRootIsDecorated(!flat && !m_entry_model->has_custom_columns());
        update_entry_path_bar();
        fit_entry_columns(m_entry_view, m_entry_model->has_custom_columns());
        if (!flat && !m_entry_model->has_custom_columns()) {
            m_entry_view->expandToDepth(6);
        }
    });
    connect(
        m_acb_cue_route_combo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this] {
            refresh_acb_cue_route_choices();
            start_acb_cue_preview();
        });
    connect(
        m_acb_include_empty_holds,
        &QCheckBox::toggled,
        this,
        [this] {
            start_acb_cue_preview();
        });
    connect(m_media.play_button, &QToolButton::clicked, this, [this] {
        if (m_audio_player == nullptr || m_audio_source_path.isEmpty()) {
            return;
        }
        if (m_audio_player->playbackState() == QMediaPlayer::PlayingState) {
            m_audio_player->pause();
        } else {
            m_audio_player->play();
        }
    });
    connect(m_media.seek_slider, &QSlider::sliderPressed, this, [this] {
        m_audio_slider_dragging = true;
        m_audio_resume_after_seek = m_audio_player != nullptr &&
            m_audio_player->playbackState() == QMediaPlayer::PlayingState;
        if (m_audio_resume_after_seek) {
            m_audio_player->pause();
        }
    });
    connect(m_media.seek_slider, &QSlider::sliderReleased, this, [this] {
        m_audio_slider_dragging = false;
        if (m_audio_player != nullptr) {
            m_audio_player->setPosition(m_media.seek_slider->value());
            if (m_audio_resume_after_seek) {
                m_audio_player->play();
            }
        }
        m_audio_resume_after_seek = false;
        update_audio_time_label();
    });
    connect(m_media.seek_slider, &QSlider::sliderMoved, this, [this](int) {
        update_audio_time_label();
    });
    connect(m_media.volume_slider, &QSlider::valueChanged, this, [this](int value) {
        if (m_audio_output != nullptr) {
            m_audio_output->setVolume(static_cast<float>(std::clamp(value, 0, 100)) / 100.0f);
        }
    });
    connect(m_media.loop_toggle, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled && m_media.loop_list->currentRow() < 0 && !m_audio_loops.empty()) {
            m_media.loop_list->setCurrentRow(0);
        }
    });
    connect(m_media.audio_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0 || preview_running() || m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr) {
            return;
        }
        const auto current = m_file_view->currentIndex();
        if (!current.isValid()) {
            return;
        }
        const auto source = m_file_proxy->mapToSource(current);
        const auto* document = ensure_loaded_document(source.row());
        if (document == nullptr) {
            return;
        }
        start_document_mux_preview(*document, m_media.audio_combo->currentData().toInt());
    });
    connect(m_media.subtitle_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_audio_player == nullptr || index < 0) {
            return;
        }
        m_audio_player->setActiveSubtitleTrack(m_media.subtitle_combo->currentData().toInt());
    });
    connect(m_file_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current) {
            const auto source = m_file_proxy->mapToSource(current);
            select_document(source.row());
            m_skip_next_file_click_reload = QApplication::mouseButtons() != Qt::NoButton;
        });
    connect(m_file_view, &QListView::clicked, this, [this](const QModelIndex& current) {
        if (!current.isValid()) {
            return;
        }
        if (m_entry_view != nullptr && m_entry_view->selectionModel() != nullptr) {
            m_entry_view->selectionModel()->clear();
            m_entry_view->selectionModel()->clearCurrentIndex();
        }
        if (m_nested_entry_view != nullptr && m_nested_entry_view->selectionModel() != nullptr) {
            m_nested_entry_view->selectionModel()->clear();
            m_nested_entry_view->selectionModel()->clearCurrentIndex();
        }
        if (m_skip_next_file_click_reload) {
            m_skip_next_file_click_reload = false;
            return;
        }
        const auto source = m_file_proxy->mapToSource(current);
        select_document(source.row());
    });
    connect(m_file_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
        [this] {
            update_file_list_status();
        });
    connect(m_doc_key_apply, &QToolButton::clicked, this, [this] {
        apply_key_panel_value(
            m_doc_key_kind,
            m_doc_key_input == nullptr ? QString{} : m_doc_key_input->text(),
            key_base_value(m_doc_key_base_input)
        );
    });
    connect(m_doc_key_input, &QLineEdit::returnPressed, this, [this] {
        apply_key_panel_value(
            m_doc_key_kind,
            m_doc_key_input == nullptr ? QString{} : m_doc_key_input->text(),
            key_base_value(m_doc_key_base_input)
        );
    });
    connect(m_preview_key_apply, &QToolButton::clicked, this, [this] {
        apply_key_panel_value(
            m_preview_key_kind,
            m_preview_key_input == nullptr ? QString{} : m_preview_key_input->text(),
            key_base_value(m_preview_key_base_input)
        );
    });
    connect(m_preview_key_input, &QLineEdit::returnPressed, this, [this] {
        apply_key_panel_value(
            m_preview_key_kind,
            m_preview_key_input == nullptr ? QString{} : m_preview_key_input->text(),
            key_base_value(m_preview_key_base_input)
        );
    });
    connect(m_doc_mux_preview_button, &QToolButton::clicked, this, [this] {
        if (m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr || !m_file_view->currentIndex().isValid()) {
            return;
        }
        if (m_entry_view != nullptr) {
            m_entry_view->clearSelection();
            if (auto* selection = m_entry_view->selectionModel(); selection != nullptr) {
                selection->clearCurrentIndex();
            }
        }
        const auto file_source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        if (const auto* document = ensure_loaded_document(file_source.row()); document != nullptr && is_mux_document(*document)) {
            start_document_mux_preview(*document, 0);
        }
    });

    connect(m_entry_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current) {
            if (!current.isValid()) {
                return;
            }

            const auto source = m_entry_proxy->mapToSource(current);
            if (const auto* summary = m_entry_model->summary_at(source); summary != nullptr) {
                if (summary->source_format == "ACB Cue") {
                    show_acb_cue(summary->source_index);
                } else if (summary->has_source) {
                    start_entry_preview(*summary);
                } else if (!summary->inspector_entries.empty()) {
                    show_entry_inspector(*summary);
                }
            }
        });
    connect(m_entry_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
        [this] {
            update_entry_selection_status();
        });
    connect(m_nested_entry_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current) {
            if (!current.isValid()) {
                return;
            }

            if (const auto* summary = m_nested_entry_model->summary_at(current); summary != nullptr) {
                if (summary->has_source) {
                    start_entry_preview(*summary);
                } else if (!summary->inspector_entries.empty()) {
                    show_entry_inspector(*summary);
                }
            }
        });

    bind_ui_text(m_file_filter, "placeholderText", "Search files");
    bind_ui_text(m_file_sort, "toolTip", "Sort loaded assets");
    bind_ui_text(m_file_type_filter, "toolTip", "Filter loaded files by detected type");
    bind_ui_text(m_left_panel_button, "toolTip", "Toggle loaded files panel");
    bind_ui_text(m_clear_files_button, "toolTip", "Unload all files");
    bind_ui_text(m_doc_mux_preview_button, "accessibleName", "Show mux preview");
    bind_ui_text(m_doc_extract_button, "toolTip", "Extract the selected loaded file");
    bind_ui_text(m_doc_extract_button, "accessibleName", "Extract selected loaded file");
    bind_ui_text(m_doc_extract_raw_button, "toolTip", "Extract the selected loaded file without decode or mux conversion");
    bind_ui_text(m_doc_extract_raw_button, "accessibleName", "Raw extract selected loaded file");
    bind_ui_text(m_entry_filter, "placeholderText", "Search entries");
    bind_ui_text(m_entry_view_mode, "toolTip", "Choose archive entry view mode");
    bind_ui_text(m_acb_cue_route_combo, "toolTip", "Choose a statically renderable cue path");
    bind_ui_text(m_acb_include_empty_holds, "text", "Include silent hold blocks");
    bind_ui_text(m_acb_include_empty_holds, "toolTip", "Render authored empty infinite holds as one finite silence block");
    bind_ui_text(m_acb_include_empty_holds, "accessibleName", "Include silent ACB hold blocks in cue preview");
    bind_ui_text(m_entry_up_button, "text", "Up");
    bind_ui_text(m_entry_up_button, "toolTip", "Go to the parent archive folder");
    bind_ui_text(m_entry_up_button, "accessibleName", "Go to parent archive folder");
    bind_ui_text(m_preview_panel_button, "toolTip", "Toggle entry preview panel");
    bind_ui_text(m_preview_extract_button, "text", "Extract Entry");
    bind_ui_text(m_preview_extract_button, "toolTip", "Extract this previewed archive entry");
    bind_ui_text(m_preview_extract_button, "accessibleName", "Extract previewed archive entry");
    bind_ui_text(m_preview_extract_raw_button, "text", "Entry Raw");
    bind_ui_text(m_preview_extract_raw_button, "toolTip", "Extract this previewed archive entry without decode or mux conversion");
    bind_ui_text(m_preview_extract_raw_button, "accessibleName", "Raw extract previewed archive entry");
    bind_ui_text(m_preview_recover_key_button, "text", "Recover HCA Key");
    bind_ui_text(m_preview_recover_key_button, "toolTip", "Recover an HCA type-56 key from this previewed file");
    bind_ui_text(m_preview_recover_key_button, "accessibleName", "Recover HCA key from previewed file");
    bind_ui_text(m_preview_recover_usm_key_button, "text", "Recover USM Key");
    bind_ui_text(m_preview_recover_usm_key_button, "toolTip", "Recover a USM mask key from audio and video evidence");
    bind_ui_text(m_preview_recover_usm_key_button, "accessibleName", "Recover USM key from previewed file");
    bind_ui_text(m_preview_recover_adx_key_button, "text", "Recover ADX Key");
    bind_ui_text(m_preview_recover_adx_key_button, "accessibleName", "Recover ADX or AHX key from previewed file");
    bind_ui_text(m_preview_recover_aac_key_button, "text", "Recover AAC Key");
    bind_ui_text(m_preview_recover_aac_key_button, "toolTip", "Recover the effective AAC key from this ACB/AWB M4A source");
    bind_ui_text(m_preview_recover_aac_key_button, "accessibleName", "Recover AAC key from previewed ACB or AWB source");
    bind_ui_text(m_media.audio_label, "text", "Audio channel");
    bind_ui_text(m_media.audio_combo, "toolTip", "Choose which stream to mux with the video preview");
    bind_ui_text(m_media.audio_popup, "toolTip", "Show mux audio choices");
    bind_ui_text(m_media.audio_popup, "accessibleName", "Show mux audio choices");
    bind_ui_text(m_media.subtitle_label, "text", "Subtitles");
    bind_ui_text(m_media.subtitle_combo, "toolTip", "Choose which subtitle language to display");
    bind_ui_text(m_media.subtitle_popup, "toolTip", "Show mux subtitle choices");
    bind_ui_text(m_media.subtitle_popup, "accessibleName", "Show mux subtitle choices");
    bind_ui_text(m_media.volume_label, "toolTip", "Volume");
    bind_ui_text(m_media.volume_label, "accessibleName", "Volume");
    bind_ui_text(m_media.volume_slider, "toolTip", "Playback volume");
    bind_ui_text(m_media.volume_slider, "accessibleName", "Playback volume");
    bind_ui_text(m_media.loop_toggle, "text", "Loop selected range");
    bind_ui_text(drop_label, "text", "Drop files or folders");
    bind_ui_text(m_cancel_extraction_button, "text", "Cancel");
    bind_ui_text(m_cancel_extraction_button, "toolTip", "Stop the current extraction");

    statusBar()->showMessage(QCoreApplication::translate("MainWindow.Chrome", "Ready"));
    update_file_sort();
    update_file_list_status();
}

void MainWindow::build_menus() {
    auto* file_menu = menuBar()->addMenu(QCoreApplication::translate("MainWindow.Chrome", "&File"));
    auto* open_files_action = file_menu->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QCoreApplication::translate("MainWindow.Chrome", "&Open Files..."));
    open_files_action->setShortcut(QKeySequence::Open);
    connect(open_files_action, &QAction::triggered, this, &MainWindow::open_files);

    auto* open_folder_action = file_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "Open &Folder..."));
    connect(open_folder_action, &QAction::triggered, this, &MainWindow::open_folder);

    file_menu->addSeparator();
    auto* new_menu = file_menu->addMenu(QCoreApplication::translate("MainWindow.Chrome", "&New"));
    auto* new_utf_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "&UTF Table"));
    connect(new_utf_action, &QAction::triggered, this, &MainWindow::new_utf_editor_document);
    auto* new_afs_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "&AFS Archive"));
    connect(new_afs_action, &QAction::triggered, this, &MainWindow::new_afs_editor_document);
    auto* new_awb_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "A&WB/AFS2 Archive"));
    connect(new_awb_action, &QAction::triggered, this, &MainWindow::new_awb_editor_document);
    auto* new_acx_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "A&CX Archive"));
    connect(new_acx_action, &QAction::triggered, this, &MainWindow::new_acx_editor_document);
    auto* new_cpk_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "C&PK Archive"));
    connect(new_cpk_action, &QAction::triggered, this, &MainWindow::new_cpk_editor_document);
    new_menu->addSeparator();
    auto* new_audio_encode_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "Audio &Encode Job"));
    connect(new_audio_encode_action, &QAction::triggered, this, &MainWindow::new_audio_encode_document);
    auto* new_usm_build_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "US&M Movie..."));
    connect(new_usm_build_action, &QAction::triggered, this, &MainWindow::new_media_build_document);
    auto* new_sfd_build_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "S&FD Movie..."));
    connect(new_sfd_build_action, &QAction::triggered, this, &MainWindow::new_sfd_build_document);
    auto* new_aax_build_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "AA&X From ADX..."));
    connect(new_aax_build_action, &QAction::triggered, this, &MainWindow::new_aax_build_document);
    auto* new_aix_build_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "A&IX From ADX..."));
    connect(new_aix_build_action, &QAction::triggered, this, &MainWindow::new_aix_build_document);
    auto* new_csb_build_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "CS&B From Folder..."));
    connect(new_csb_build_action, &QAction::triggered, this, &MainWindow::new_csb_build_document);
    auto* new_cvm_script_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "CVM From C&VS..."));
    connect(new_cvm_script_action, &QAction::triggered, this, &MainWindow::new_cvm_from_script_document);
    auto* new_cvm_directory_action = new_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "CVM From F&older..."));
    connect(new_cvm_directory_action, &QAction::triggered, this, &MainWindow::new_cvm_from_directory_document);

    file_menu->addSeparator();
    auto* extract_all_action = file_menu->addAction(make_action_icon(ActionGlyph::Extract), QCoreApplication::translate("MainWindow.Chrome", "Extract &All..."));
    connect(extract_all_action, &QAction::triggered, this, [this] {
        start_extraction(all_file_targets(), ExtractionMode::Decoded);
    });
    auto* extract_all_raw_action = file_menu->addAction(make_action_icon(ActionGlyph::RawExtract), QCoreApplication::translate("MainWindow.Chrome", "Extract All &Raw..."));
    connect(extract_all_raw_action, &QAction::triggered, this, [this] {
        start_extraction(all_file_targets(), ExtractionMode::Raw);
    });
    auto* recover_hca_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QCoreApplication::translate("MainWindow.Chrome", "Recover HCA Keys from All Loaded Files"));
    connect(recover_hca_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_recovery_sources();
        const auto count = sources.size();
        start_hca_key_recovery(
            std::move(sources),
            QCoreApplication::translate("MainWindow.Chrome", "%n loaded file(s)", nullptr, static_cast<int>(count)));
    });
    auto* recover_aac_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QCoreApplication::translate("MainWindow.Chrome", "Recover AAC Keys from Loaded ACB/AWB Files"));
    connect(recover_aac_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_aac_recovery_sources();
        const auto count = sources.size();
        start_aac_key_recovery(
            std::move(sources),
            QCoreApplication::translate("MainWindow.Chrome", "%n loaded ACB/AWB file(s)", nullptr, static_cast<int>(count)));
    });
    auto* recover_usm_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QCoreApplication::translate("MainWindow.Chrome", "Recover USM Keys from All Loaded Files"));
    connect(recover_usm_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_usm_recovery_sources();
        const auto count = sources.size();
        start_usm_key_recovery(
            std::move(sources),
            QCoreApplication::translate("MainWindow.Chrome", "%n loaded file(s)", nullptr, static_cast<int>(count)));
    });
    auto* recover_adx_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QCoreApplication::translate("MainWindow.Chrome", "Recover ADX Keys from All Loaded Files"));
    connect(recover_adx_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_adx_recovery_sources();
        const auto count = sources.size();
        start_adx_key_recovery(
            std::move(sources),
            AdxRecoveryKind::Adx,
            QCoreApplication::translate("MainWindow.Chrome", "%n loaded file(s)", nullptr, static_cast<int>(count)));
    });
    auto* recover_ahx_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QCoreApplication::translate("MainWindow.Chrome", "Recover AHX Keys from All Loaded Files"));
    connect(recover_ahx_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_adx_recovery_sources();
        const auto count = sources.size();
        start_adx_key_recovery(
            std::move(sources),
            AdxRecoveryKind::Ahx,
            QCoreApplication::translate("MainWindow.Chrome", "%n loaded file(s)", nullptr, static_cast<int>(count)));
    });

    file_menu->addSeparator();
    auto* clear_action = file_menu->addAction(make_action_icon(ActionGlyph::Clear), QCoreApplication::translate("MainWindow.Chrome", "&Clear Loaded Files"));
    connect(clear_action, &QAction::triggered, this, &MainWindow::clear_loaded_files);

    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "E&xit"));
    exit_action->setShortcut(QKeySequence::Quit);
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    m_edit_menu = menuBar()->addMenu(QCoreApplication::translate("MainWindow.Chrome", "&Edit"));
    m_decryption_keys_action = m_edit_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "&Cryptography Keys"));
    connect(m_decryption_keys_action, &QAction::triggered, this, &MainWindow::show_decryption_keys_panel);
    m_edit_menu->addSeparator();
    m_extract_mux_outputs_action = m_edit_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "Extract USM/SFD &Mux Outputs"));
    m_extract_mux_outputs_action->setCheckable(true);
    m_extract_mux_outputs_action->setChecked(true);
    m_extract_acb_cues_action = m_edit_menu->addAction(
        QCoreApplication::translate(
            "MainWindow.Chrome",
            "Extract ACBs as &Rendered Cues"));
    m_extract_acb_cues_action->setCheckable(true);
    m_extract_acb_cues_action->setChecked(false);

    auto* view_menu = menuBar()->addMenu(QCoreApplication::translate("MainWindow.Chrome", "&View"));
    auto* theme_group = new QActionGroup(this);
    m_light_theme_action = view_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "&Light Mode"));
    m_dark_theme_action = view_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "&Dark Mode"));
    m_light_theme_action->setCheckable(true);
    m_dark_theme_action->setCheckable(true);
    theme_group->addAction(m_light_theme_action);
    theme_group->addAction(m_dark_theme_action);
    m_light_theme_action->setChecked(m_theme == Theme::Light);
    m_dark_theme_action->setChecked(m_theme == Theme::Dark);
    connect(m_light_theme_action, &QAction::triggered, this, [this] { set_theme(Theme::Light); });
    connect(m_dark_theme_action, &QAction::triggered, this, [this] { set_theme(Theme::Dark); });
    view_menu->addSeparator();
    QSettings ui_settings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
    auto* language_menu = view_menu->addMenu(QCoreApplication::translate("MainWindow.Chrome", "&Language"));
    m_language_group = new QActionGroup(language_menu);
    m_language_group->setExclusive(true);
    const auto current_language = i18n::TranslationManager::instance().selected_code();
    const auto add_language_action = [
        this,
        language_menu,
        &current_language
    ](const QString& code, const char* source) {
        auto* action = language_menu->addAction(
            QCoreApplication::translate("MainWindow.Chrome", source)
        );
        action->setCheckable(true);
        action->setData(code);
        action->setChecked(current_language == code);
        m_language_group->addAction(action);
        bind_ui_text(action, "text", source);
        connect(action, &QAction::triggered, this, [this, code](bool checked) {
            if (!checked) {
                return;
            }
            m_pending_language_code = code;
            apply_pending_language();
        });
    };
    // Language codes are stable settings values. Labels are localized autonyms.
    add_language_action(QStringLiteral("system"), "System Default");
    language_menu->addSeparator();
    add_language_action(QStringLiteral("en"), "English");
    add_language_action(QStringLiteral("es"), "Español");
    add_language_action(QStringLiteral("ja"), "日本語");
    add_language_action(QStringLiteral("zh_CN"), "简体中文");
    add_language_action(QStringLiteral("zh_TW"), "繁體中文");
    view_menu->addSeparator();
    m_compact_lists_action = view_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "&Compact Lists"));
    m_compact_lists_action->setCheckable(true);
    m_compact_lists_action->setChecked(ui_settings.value(QStringLiteral("ui/compactLists"), false).toBool());
    connect(m_compact_lists_action, &QAction::toggled, this, &MainWindow::set_compact_lists);
    set_compact_lists(m_compact_lists_action->isChecked());

    auto* window_menu = menuBar()->addMenu(QCoreApplication::translate("MainWindow.Chrome", "&Window"));
    m_toggle_left_action = window_menu->addAction(make_sidebar_icon(true), QCoreApplication::translate("MainWindow.Chrome", "Loaded Files Panel"));
    m_toggle_left_action->setCheckable(true);
    m_toggle_left_action->setChecked(true);
    connect(m_toggle_left_action, &QAction::triggered, this, [this](bool checked) {
        if (m_left_panel_button != nullptr) {
            m_left_panel_button->setChecked(checked);
        }
        toggle_left_panel();
    });

    m_toggle_preview_action = window_menu->addAction(make_sidebar_icon(false), QCoreApplication::translate("MainWindow.Chrome", "Entry Preview Panel"));
    m_toggle_preview_action->setCheckable(true);
    m_toggle_preview_action->setChecked(false);
    connect(m_toggle_preview_action, &QAction::triggered, this, [this](bool checked) {
        if (m_preview_panel_button != nullptr) {
            m_preview_panel_button->setChecked(checked);
        }
        toggle_preview_panel();
    });

    window_menu->addSeparator();
    m_always_show_access_keys_action = window_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "Always Show &Access Keys"));
    m_always_show_access_keys_action->setCheckable(true);
    m_always_show_access_keys_action->setChecked(qApp->property("alwaysShowAccessKeys").toBool());
    connect(m_always_show_access_keys_action, &QAction::toggled, this, [](bool visible) {
        qApp->setProperty("alwaysShowAccessKeys", visible);
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (widget != nullptr) {
                widget->update();
            }
        }
    });

    window_menu->addSeparator();
    auto* lock_left_shelf_action = window_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "Lock Loaded Files Shelf"));
    lock_left_shelf_action->setCheckable(true);
    connect(lock_left_shelf_action, &QAction::triggered, this, [this](bool locked) {
        if (m_left_edge_rail != nullptr) {
            static_cast<AutoHideRail*>(m_left_edge_rail)->set_auto_hide_enabled(!locked);
        }
    });

    auto* lock_preview_shelf_action = window_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "Lock Preview Shelf"));
    lock_preview_shelf_action->setCheckable(true);
    connect(lock_preview_shelf_action, &QAction::triggered, this, [this](bool locked) {
        if (m_right_edge_rail != nullptr) {
            static_cast<AutoHideRail*>(m_right_edge_rail)->set_auto_hide_enabled(!locked);
        }
    });

    auto* lock_workspace_ribbon_action = window_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "Lock Browse/Editor Ribbon"));
    lock_workspace_ribbon_action->setCheckable(true);
    lock_workspace_ribbon_action->setChecked(true);
    connect(lock_workspace_ribbon_action, &QAction::triggered, this, [this](bool locked) {
        if (m_workspace_tabs != nullptr) {
            static_cast<WorkspaceTabWidget*>(m_workspace_tabs)->set_ribbon_locked(locked);
        }
    });
    if (m_workspace_tabs != nullptr) {
        static_cast<WorkspaceTabWidget*>(m_workspace_tabs)->set_ribbon_locked(true);
    }

    window_menu->addSeparator();
    auto* reset_layout_action = window_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "&Reset Layout"));
    connect(reset_layout_action, &QAction::triggered, this, [this] {
        if (m_toggle_left_action != nullptr) {
            m_toggle_left_action->setChecked(true);
        }
        if (m_left_panel_button != nullptr) {
            m_left_panel_button->setChecked(true);
        }
        toggle_left_panel();
        const auto preview_open = m_preview_panel_button != nullptr && m_preview_panel_button->isChecked();
        if (m_toggle_preview_action != nullptr) {
            m_toggle_preview_action->setChecked(preview_open);
        }
        toggle_preview_panel();
        m_splitter->setSizes(preview_open ? QList<int>{320, 900, 640} : QList<int>{320, 1120, 0});
        QSettings settings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
        settings.remove(QStringLiteral("ui"));
    });

    auto* help_menu = menuBar()->addMenu(QCoreApplication::translate("MainWindow.Chrome", "&Help"));
    auto* log_action = help_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "Log File Location"));
    connect(log_action, &QAction::triggered, this, [this] {
        statusBar()->showMessage(log_path(), 8000);
    });
    auto* about_action = help_menu->addAction(QCoreApplication::translate("MainWindow.Chrome", "&About CriStudio"));
    connect(about_action, &QAction::triggered, this, [this] {
        constexpr auto repository_url = "https://github.com/Youjose/CriCodecs";

        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("AboutCriStudioDialog"));
        dialog.setWindowTitle(QCoreApplication::translate("MainWindow.Chrome", "About CriStudio"));
        dialog.setWindowIcon(windowIcon());
        dialog.setModal(true);
        dialog.setMinimumWidth(480);

        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(24, 22, 24, 18);
        layout->setSpacing(12);

        auto* title = new QLabel(app_title(), &dialog);
        auto title_font = title->font();
        title_font.setPointSizeF(title_font.pointSizeF() * 1.25);
        title_font.setWeight(QFont::DemiBold);
        title->setFont(title_font);
        title->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(title);

        const auto core_version = QString::fromLatin1(
            cricodecs::version.data(),
            static_cast<qsizetype>(cricodecs::version.size()));
        auto* core = new QLabel(
            QCoreApplication::translate("MainWindow.Chrome", "Powered by CriCodecs %1.").arg(core_version),
            &dialog);
        core->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(core);

        auto* description = new QLabel(
            QCoreApplication::translate(
                "MainWindow.Chrome",
                "CriStudio is the desktop interface for inspecting, decoding, and building CRI middleware formats with the native CriCodecs library."),
            &dialog);
        description->setWordWrap(true);
        description->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(description);

        auto* repository = new QLabel(
            QCoreApplication::translate("MainWindow.Chrome", "Repository and issue tracker: <a href=\"%1\">%1</a>")
                .arg(QString::fromLatin1(repository_url)),
            &dialog);
        repository->setTextFormat(Qt::RichText);
        repository->setTextInteractionFlags(Qt::TextBrowserInteraction);
        repository->setOpenExternalLinks(true);
        repository->setWordWrap(true);
        layout->addWidget(repository);

        auto* issues = new QLabel(
            QCoreApplication::translate(
                "MainWindow.Chrome",
                "For bugs, feature requests, or other problems, please open an issue in the repository."),
            &dialog);
        issues->setWordWrap(true);
        issues->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(issues);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        auto* repository_button = buttons->addButton(
            QCoreApplication::translate("MainWindow.Chrome", "Open Repository"),
            QDialogButtonBox::ActionRole);
        connect(repository_button, &QPushButton::clicked, &dialog, [repository_url] {
            QDesktopServices::openUrl(QUrl(QString::fromLatin1(repository_url)));
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        dialog.exec();
    });

    bind_ui_text(file_menu->menuAction(), "text", "&File");
    bind_ui_text(open_files_action, "text", "&Open Files...");
    bind_ui_text(open_folder_action, "text", "Open &Folder...");
    bind_ui_text(new_menu->menuAction(), "text", "&New");
    bind_ui_text(new_utf_action, "text", "&UTF Table");
    bind_ui_text(new_afs_action, "text", "&AFS Archive");
    bind_ui_text(new_awb_action, "text", "A&WB/AFS2 Archive");
    bind_ui_text(new_acx_action, "text", "A&CX Archive");
    bind_ui_text(new_cpk_action, "text", "C&PK Archive");
    bind_ui_text(new_audio_encode_action, "text", "Audio &Encode Job");
    bind_ui_text(new_usm_build_action, "text", "US&M Movie...");
    bind_ui_text(new_sfd_build_action, "text", "S&FD Movie...");
    bind_ui_text(new_aax_build_action, "text", "AA&X From ADX...");
    bind_ui_text(new_aix_build_action, "text", "A&IX From ADX...");
    bind_ui_text(new_csb_build_action, "text", "CS&B From Folder...");
    bind_ui_text(new_cvm_script_action, "text", "CVM From C&VS...");
    bind_ui_text(new_cvm_directory_action, "text", "CVM From F&older...");
    bind_ui_text(extract_all_action, "text", "Extract &All...");
    bind_ui_text(extract_all_raw_action, "text", "Extract All &Raw...");
    bind_ui_text(recover_hca_keys_action, "text", "Recover HCA Keys from All Loaded Files");
    bind_ui_text(recover_aac_keys_action, "text", "Recover AAC Keys from Loaded ACB/AWB Files");
    bind_ui_text(recover_usm_keys_action, "text", "Recover USM Keys from All Loaded Files");
    bind_ui_text(recover_adx_keys_action, "text", "Recover ADX Keys from All Loaded Files");
    bind_ui_text(recover_ahx_keys_action, "text", "Recover AHX Keys from All Loaded Files");
    bind_ui_text(clear_action, "text", "&Clear Loaded Files");
    bind_ui_text(exit_action, "text", "E&xit");
    bind_ui_text(m_edit_menu->menuAction(), "text", "&Edit");
    bind_ui_text(m_decryption_keys_action, "text", "&Cryptography Keys");
    bind_ui_text(m_extract_mux_outputs_action, "text", "Extract USM/SFD &Mux Outputs");
    bind_ui_text(m_extract_acb_cues_action, "text", "Extract ACBs as &Rendered Cues");
    bind_ui_text(view_menu->menuAction(), "text", "&View");
    bind_ui_text(m_light_theme_action, "text", "&Light Mode");
    bind_ui_text(m_dark_theme_action, "text", "&Dark Mode");
    bind_ui_text(language_menu->menuAction(), "text", "&Language");
    bind_ui_text(m_compact_lists_action, "text", "&Compact Lists");
    bind_ui_text(window_menu->menuAction(), "text", "&Window");
    bind_ui_text(m_toggle_left_action, "text", "Loaded Files Panel");
    bind_ui_text(m_toggle_preview_action, "text", "Entry Preview Panel");
    bind_ui_text(m_always_show_access_keys_action, "text", "Always Show &Access Keys");
    bind_ui_text(lock_left_shelf_action, "text", "Lock Loaded Files Shelf");
    bind_ui_text(lock_preview_shelf_action, "text", "Lock Preview Shelf");
    bind_ui_text(lock_workspace_ribbon_action, "text", "Lock Browse/Editor Ribbon");
    bind_ui_text(reset_layout_action, "text", "&Reset Layout");
    bind_ui_text(help_menu->menuAction(), "text", "&Help");
    bind_ui_text(log_action, "text", "Log File Location");
    bind_ui_text(about_action, "text", "&About CriStudio");

    m_memory_usage_label = new QLabel(this);
    m_memory_usage_label->setObjectName(QStringLiteral("MemoryUsageLabel"));
    m_memory_usage_label->setMinimumWidth(96);
    m_memory_usage_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_memory_usage_label->setToolTip(QCoreApplication::translate("MainWindow.Chrome", "Resident process memory"));
    bind_ui_text(m_memory_usage_label, "toolTip", "Resident process memory");
    menuBar()->setCornerWidget(m_memory_usage_label, Qt::TopRightCorner);

    m_memory_usage_timer = new QTimer(this);
    connect(m_memory_usage_timer, &QTimer::timeout, this, &MainWindow::update_memory_usage_label);
    m_memory_usage_timer->start(2000);
    update_memory_usage_label();
}

void MainWindow::update_memory_usage_label() {
    if (m_memory_usage_label == nullptr) {
        return;
    }
    const auto bytes = resident_memory_bytes();
    if (!bytes) {
        m_memory_usage_label->setText(QCoreApplication::translate("MainWindow.Chrome", "Mem --"));
        return;
    }
    m_memory_usage_label->setText(QCoreApplication::translate("MainWindow.Chrome", "Mem %1").arg(format_memory_size(*bytes)));
}

void MainWindow::toggle_left_panel() {
    if (m_left_panel == nullptr || m_toggle_left_action == nullptr) {
        return;
    }
    const auto expanded = m_toggle_left_action->isChecked();
    if (expanded) {
        m_left_panel->setMaximumWidth(QWIDGETSIZE_MAX);
        m_left_panel->setMinimumWidth(260);
        m_left_panel->show();
        if (m_splitter != nullptr) {
            const auto sizes = m_splitter->sizes();
            if (!sizes.empty() && sizes[0] < 260) {
                const auto left_width = std::max(260, m_left_panel_width);
                m_splitter->setSizes({
                    left_width,
                    std::max(0, sizes.value(1, 900) - left_width),
                    sizes.value(2, 0)
                });
            }
        }
    } else {
        if (m_splitter != nullptr) {
            const auto sizes = m_splitter->sizes();
            if (!sizes.empty() && sizes[0] >= 260) {
                m_left_panel_width = sizes[0];
            }
        }
        m_left_panel->setMinimumWidth(0);
        m_left_panel->hide();
    }
    schedule_position_edge_buttons();
}

void MainWindow::position_edge_buttons() {
    if (m_left_panel_button != nullptr) {
        m_left_panel_button->raise();
        m_left_panel_button->show();
    }
    if (m_preview_panel_button != nullptr) {
        m_preview_panel_button->raise();
        m_preview_panel_button->show();
    }
}

void MainWindow::schedule_position_edge_buttons() {
    position_edge_buttons();
    QTimer::singleShot(0, this, [this] {
        position_edge_buttons();
    });
}

void MainWindow::set_theme(Theme theme) {
    m_theme = theme;
    if (m_light_theme_action != nullptr) {
        const QSignalBlocker blocker(m_light_theme_action);
        m_light_theme_action->setChecked(theme == Theme::Light);
    }
    if (m_dark_theme_action != nullptr) {
        const QSignalBlocker blocker(m_dark_theme_action);
        m_dark_theme_action->setChecked(theme == Theme::Dark);
    }
    if (theme == Theme::Dark) {
        QApplication::setPalette(dark_palette());
        qApp->setStyleSheet(visual_stylesheet(true));
    } else {
        QApplication::setPalette(light_palette());
        qApp->setStyleSheet(visual_stylesheet(false));
    }

    const auto left_icon = make_sidebar_icon(true);
    const auto right_icon = make_sidebar_icon(false);
    const auto clear_icon = make_action_icon(ActionGlyph::Clear);
    const auto extract_icon = make_action_icon(ActionGlyph::Extract);
    const auto raw_icon = make_action_icon(ActionGlyph::RawExtract);
    const auto recover_key_icon = make_action_icon(ActionGlyph::RecoverKey);
    const auto mux_icon = make_action_icon(ActionGlyph::MuxPreview);
    if (m_left_panel_button != nullptr) {
        m_left_panel_button->setIcon(left_icon);
    }
    if (m_clear_files_button != nullptr) {
        m_clear_files_button->setIcon(clear_icon);
    }
    if (m_preview_panel_button != nullptr) {
        m_preview_panel_button->setIcon(right_icon);
    }
    if (m_doc_extract_button != nullptr) {
        m_doc_extract_button->setIcon(extract_icon);
    }
    if (m_doc_extract_raw_button != nullptr) {
        m_doc_extract_raw_button->setIcon(raw_icon);
    }
    if (m_doc_mux_preview_button != nullptr) {
        m_doc_mux_preview_button->setIcon(mux_icon);
    }
    if (m_preview_extract_button != nullptr) {
        m_preview_extract_button->setIcon(extract_icon);
    }
    if (m_preview_extract_raw_button != nullptr) {
        m_preview_extract_raw_button->setIcon(raw_icon);
    }
    if (m_preview_recover_key_button != nullptr) {
        m_preview_recover_key_button->setIcon(recover_key_icon);
    }
    if (m_preview_recover_usm_key_button != nullptr) {
        m_preview_recover_usm_key_button->setIcon(recover_key_icon);
    }
    if (m_preview_recover_adx_key_button != nullptr) {
        m_preview_recover_adx_key_button->setIcon(recover_key_icon);
    }
    if (m_preview_recover_aac_key_button != nullptr) {
        m_preview_recover_aac_key_button->setIcon(recover_key_icon);
    }
    if (m_toggle_left_action != nullptr) {
        m_toggle_left_action->setIcon(left_icon);
    }
    if (m_toggle_preview_action != nullptr) {
        m_toggle_preview_action->setIcon(right_icon);
    }
}



} // namespace cristudio
