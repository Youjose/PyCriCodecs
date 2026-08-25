#include "shared/i18n.hpp"
#include "ui_helpers.hpp"

#include "path_text.hpp"

#include <QCoreApplication>
#include <QAbstractAnimation>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QEasingCurve>
#include <QEventLoop>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QDesktopServices>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QHBoxLayout>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QShortcut>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QStringList>

#include <algorithm>
#include <bit>
#include <iterator>
#include <unordered_map>
#include <utility>

#ifndef CRISTUDIO_SOURCE_REVISION
#define CRISTUDIO_SOURCE_REVISION "unknown"
#endif

namespace cristudio {

QString archive_basename(QString text) {
    text.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (text.endsWith(QLatin1Char('/'))) {
        text.chop(1);
    }
    const auto slash = text.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 && slash + 1 < text.size() ? text.mid(slash + 1) : text;
}

QString strip_mux_prefix(QString text) {
    // Internal synthetic-entry prefix; it is parsed and must never be translated.
    constexpr auto prefix = "Mux preview/";
    return text.startsWith(QLatin1String(prefix))
        ? text.mid(static_cast<int>(std::char_traits<char>::length(prefix)))
        : text;
}

QString recovery_key_text(uint64_t key, int digits) {
    return QStringLiteral("0x%1").arg(
        QString::number(static_cast<qulonglong>(key), 16).toUpper().rightJustified(digits, QLatin1Char('0')));
}

QString recovery_source_label(
    std::string_view name,
    const std::filesystem::path& path,
    const char* translation_context) {
    if (!name.empty()) {
        return utf8_to_qstring(name);
    }
    if (!path.empty()) {
        return path_to_qstring(path.filename());
    }
    return QCoreApplication::translate(translation_context, "Selected entry");
}

void reveal_in_file_manager(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return;
    }
#if defined(Q_OS_WIN)
    const QStringList arguments{
        QStringLiteral("/select,"),
        QDir::toNativeSeparators(info.absoluteFilePath())
    };
    QProcess::startDetached(QStringLiteral("explorer.exe"), arguments);
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), info.absoluteFilePath()});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath()));
#endif
}


QPalette dark_palette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(35, 37, 41));
    palette.setColor(QPalette::WindowText, QColor(235, 235, 235));
    palette.setColor(QPalette::Base, QColor(24, 25, 28));
    palette.setColor(QPalette::AlternateBase, QColor(42, 44, 49));
    palette.setColor(QPalette::ToolTipBase, QColor(235, 235, 235));
    palette.setColor(QPalette::ToolTipText, QColor(24, 25, 28));
    palette.setColor(QPalette::Text, QColor(235, 235, 235));
    palette.setColor(QPalette::Button, QColor(45, 48, 54));
    palette.setColor(QPalette::ButtonText, QColor(235, 235, 235));
    palette.setColor(QPalette::BrightText, QColor(255, 80, 80));
    palette.setColor(QPalette::Mid, QColor(169, 178, 173));
    palette.setColor(QPalette::Midlight, QColor(193, 201, 197));
    palette.setColor(QPalette::Highlight, QColor(53, 132, 228));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    return palette;
}

QPalette light_palette() {
    QPalette palette = QApplication::style()->standardPalette();
    palette.setColor(QPalette::Window, QColor(240, 244, 242));
    palette.setColor(QPalette::WindowText, QColor(31, 36, 35));
    palette.setColor(QPalette::Base, QColor(248, 250, 247));
    palette.setColor(QPalette::AlternateBase, QColor(242, 246, 243));
    palette.setColor(QPalette::ToolTipBase, QColor(31, 36, 35));
    palette.setColor(QPalette::ToolTipText, QColor(248, 250, 247));
    palette.setColor(QPalette::Text, QColor(31, 36, 35));
    palette.setColor(QPalette::Button, QColor(232, 239, 235));
    palette.setColor(QPalette::ButtonText, QColor(31, 36, 35));
    palette.setColor(QPalette::Mid, QColor(150, 158, 151));
    palette.setColor(QPalette::Highlight, QColor(37, 132, 153));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    return palette;
}

QString visual_stylesheet(bool dark) {
    const auto window = dark ? QStringLiteral("#202426") : QStringLiteral("#f4f6f5");
    const auto surface = dark ? QStringLiteral("#181b1d") : QStringLiteral("#ffffff");
    const auto surfaceAlt = dark ? QStringLiteral("#272d2f") : QStringLiteral("#f0f3f2");
    const auto raised = dark ? QStringLiteral("#303739") : QStringLiteral("#e9eeec");
    const auto border = dark ? QStringLiteral("#40494d") : QStringLiteral("#cbd4d0");
    const auto borderSoft = dark ? QStringLiteral("#323a3d") : QStringLiteral("#dce3e0");
    const auto text = dark ? QStringLiteral("#edf1ef") : QStringLiteral("#202725");
    const auto muted = dark ? QStringLiteral("#aab4af") : QStringLiteral("#69766f");
    const auto accent = dark ? QStringLiteral("#59bac7") : QStringLiteral("#157f8f");
    const auto accentSoft = dark ? QStringLiteral("#173b41") : QStringLiteral("#dff0f2");
    const auto mono = dark ? QStringLiteral("#d5efe8") : QStringLiteral("#123d42");
    const auto scrollbarTrack = dark ? QStringLiteral("#202629") : QStringLiteral("#e9efec");
    const auto scrollbarHandle = dark ? QStringLiteral("#566569") : QStringLiteral("#aebbb4");
    const auto scrollbarHandleHover = dark ? QStringLiteral("#58b7c5") : QStringLiteral("#6e9fa8");

    auto css = QStringLiteral(R"(
        QMainWindow, QDialog, QMessageBox, QWidget#AppChrome {
            background: @window;
            color: @text;
        }
        QMenuBar {
            background: @chrome;
            color: @text;
            border-bottom: 1px solid @border;
            padding: 4px 8px;
        }
        QMenuBar::item {
            background: transparent;
            border-radius: 3px;
            padding: 4px 8px;
        }
        QMenuBar::item:selected {
            background: @accentSoft;
            color: @text;
        }
        QLabel#MemoryUsageLabel {
            color: @muted;
            padding: 4px 10px;
            font-family: monospace;
        }
        QWidget#SideRail, QWidget#PreviewRail {
            background: @chrome;
            border: 0;
        }
        QWidget#EdgeHoverBand {
            background: transparent;
            border: 0;
        }
        QWidget#DocumentPanel {
            background: @window;
        }
        QWidget#LoadedPanel {
            background: @surface;
            border-right: 1px solid @borderSoft;
        }
        QWidget#PreviewPanel {
            background: @surface;
            border-left: 1px solid @borderSoft;
        }
        QDialog#DecryptionKeysWindow, QWidget#DecryptionKeysPanel {
            background: @window;
            color: @text;
        }
        QFrame#DropOverlay {
            background: rgba(@dropOverlayRgb, 210);
            border: 1px solid @accent;
            border-radius: 10px;
        }
        QDialog#RecoveryNotification {
            background: @surface;
            border: 1px solid @accent;
            border-radius: 7px;
        }
        QDialog#RecoveryNotification[severity="error"] {
            border-color: #c65b5b;
        }
        QLabel#RecoveryNotificationTitle {
            color: @text;
            font-size: 14px;
            font-weight: 650;
        }
        QLabel#RecoveryNotificationSummary {
            color: @text;
            font-size: 13px;
        }
        QPlainTextEdit#RecoveryNotificationDetails {
            background: @surfaceAlt;
            border-color: @borderSoft;
            font-family: monospace;
        }
        QTreeWidget#RecoveryResultTable {
            border-color: @borderSoft;
        }
        QTreeWidget#RecoveryResultTable::item {
            padding: 3px 5px;
        }
        QToolButton#RecoveryNotificationClose {
            background: transparent;
            border: 0;
            color: @muted;
            font-size: 16px;
            padding: 1px 5px;
        }
        QToolButton#RecoveryNotificationClose:hover {
            background: @raised;
            color: @text;
        }
        QLabel#DropOverlayLabel {
            color: @text;
            font-size: 18px;
            font-weight: 600;
        }
        QWidget#AudioPanel {
            background: @surfaceAlt;
            border: 1px solid @border;
            border-radius: 7px;
        }
        QWidget#VideoFrame {
            background: #000000;
            border: 1px solid @border;
            border-radius: 7px;
        }
        QTabWidget#PreviewTabs, QTabWidget#WorkspaceTabs, QTabWidget#EditorTabs, QTabWidget#EditorPreviewTabs {
            background: transparent;
        }
        QTabWidget#WorkspaceTabs::pane {
            border: 0;
            background: @window;
        }
        QTabWidget#WorkspaceTabs QTabBar#WorkspaceShelfTabs {
            background: @chrome;
            border-bottom: 1px solid @borderSoft;
        }
        QTabWidget#WorkspaceTabs QTabBar#WorkspaceShelfTabs::tab {
            background: transparent;
            color: @muted;
            border: 0;
            border-bottom: 2px solid transparent;
            min-width: 76px;
            padding: 6px 12px;
        }
        QTabWidget#WorkspaceTabs QTabBar#WorkspaceShelfTabs::tab:hover {
            background: @accentSoft;
            color: @text;
        }
        QTabWidget#WorkspaceTabs QTabBar#WorkspaceShelfTabs::tab:selected {
            background: transparent;
            border-bottom: 2px solid @accent;
            color: @text;
        }
        QTabWidget#EditorTabs::pane {
            border: 0;
            border-top: 1px solid @borderSoft;
        }
        QTabWidget#EditorTabs QTabBar::tab {
            background: transparent;
            color: @muted;
            border: 0;
            border-bottom: 2px solid transparent;
            min-width: 120px;
            max-width: 240px;
            padding: 6px 10px;
            margin-right: 2px;
        }
        QTabWidget#EditorTabs QTabBar::tab:hover {
            background: @surfaceAlt;
            color: @text;
        }
        QTabWidget#EditorTabs QTabBar::tab:selected {
            color: @text;
            border-bottom: 2px solid @accent;
        }
        QTabWidget#PreviewTabs::pane, QTabWidget#EditorPreviewTabs::pane {
            border: 0;
            border-top: 1px solid @borderSoft;
        }
        QTabWidget#PreviewTabs QTabBar::tab, QTabWidget#EditorPreviewTabs QTabBar::tab {
            background: @surfaceAlt;
            color: @muted;
            border: 1px solid @borderSoft;
            border-bottom: 0;
            border-top-left-radius: 3px;
            border-top-right-radius: 3px;
            min-width: 88px;
            max-width: 190px;
            padding: 4px 10px;
            margin-right: 3px;
        }
        QTabWidget#PreviewTabs QTabBar::tab:hover, QTabWidget#EditorPreviewTabs QTabBar::tab:hover {
            background: @accentSoft;
            color: @text;
        }
        QTabWidget#PreviewTabs QTabBar::tab:selected, QTabWidget#EditorPreviewTabs QTabBar::tab:selected {
            background: @surface;
            color: @text;
            border-top: 2px solid @accent;
            border-bottom: 1px solid @surface;
            margin-bottom: -1px;
            padding-top: 3px;
        }
        QToolButton#EditorTabCloseButton {
            background: transparent;
            border: 0;
            border-radius: 3px;
            color: @muted;
            min-width: 18px;
            max-width: 18px;
            min-height: 18px;
            max-height: 18px;
            padding: 0;
        }
        QToolButton#EditorTabCloseButton:hover {
            background: @raised;
            color: @text;
        }
        QWidget#WorkspaceShelfHoverBand {
            background: transparent;
            border: 0;
        }
        QMenu {
            background: @surface;
            color: @text;
            border: 1px solid @border;
            padding: 4px;
        }
        QMenu::item {
            border-radius: 3px;
            padding: 5px 24px 5px 9px;
        }
        QMenuBar::item:selected, QMenu::item:selected {
            background: @accentSoft;
            color: @text;
        }
        QMenu::separator {
            background: @borderSoft;
            height: 1px;
            margin: 4px 7px;
        }
        QStatusBar {
            background: @chrome;
            border-top: 1px solid @borderSoft;
            color: @muted;
        }
        QLabel#DocumentTitle, QLabel#PreviewTitle {
            color: @text;
            font-size: 18px;
            font-weight: 650;
        }
        QLabel#DocumentSubtitle, QLabel#PreviewSubtitle, QLabel#AudioStatus {
            color: @muted;
        }
        QLabel#FileListStatus, QLabel#EntrySelectionStatus, QLabel#LoadingStatus {
            color: @muted;
            font-size: 12px;
        }
        QLabel#DimLabel {
            color: @muted;
            font-weight: 500;
        }
        QLabel#ValueLabel {
            color: @text;
        }
        QLineEdit {
            background: @surface;
            color: @text;
            border: 1px solid @border;
            border-radius: 4px;
            padding: 6px 8px;
            selection-background-color: @accent;
            selection-color: white;
        }
        QLineEdit#SearchField {
            background: @surfaceAlt;
            border-color: @borderSoft;
            min-height: 22px;
            font-weight: 500;
        }
        QLineEdit#SearchField:focus {
            background: @surface;
            border-color: @accent;
        }
        QComboBox {
            background: @surface;
            color: @text;
            border: 1px solid @border;
            border-radius: 4px;
            padding: 4px 28px 4px 7px;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            border-left: 1px solid @borderSoft;
            width: 24px;
        }
        QComboBox#KeyModeCombo {
            background: @raised;
            border-color: @accent;
            font-weight: 500;
        }
        QComboBox#KeyModeCombo, QComboBox#FileSortCombo, QComboBox#FileTypeFilterCombo, QComboBox#EntryViewModeCombo, QComboBox#MuxAudioCombo, QComboBox#MuxSubtitleCombo {
            background: @raised;
            border-color: @borderSoft;
            padding: 5px 28px 5px 9px;
        }
        QComboBox#KeyModeCombo::drop-down, QComboBox#FileSortCombo::drop-down, QComboBox#FileTypeFilterCombo::drop-down, QComboBox#EntryViewModeCombo::drop-down, QComboBox#MuxAudioCombo::drop-down, QComboBox#MuxSubtitleCombo::drop-down {
            border-left: 1px solid @borderSoft;
            width: 24px;
        }
        QLabel#EntryPathLabel {
            background: @surfaceAlt;
            border: 1px solid @borderSoft;
            border-radius: 4px;
            color: @muted;
            font-size: 12px;
            font-weight: 500;
            padding: 5px 9px;
        }
        QLabel#EntryPathLabel a {
            color: @text;
            text-decoration: none;
        }
        QWidget#KeyBaseSelector, QWidget#OffsetBaseSelector, QWidget#EntryViewModeSelector {
            background: @surface;
            border: 1px solid @border;
            border-radius: 5px;
        }
        QToolButton#KeyBaseSegment, QToolButton#OffsetBaseSegment, QToolButton#EntryViewModeSegment {
            background: transparent;
            color: @muted;
            border: 0;
            border-radius: 4px;
            padding: 4px 7px;
            min-width: 30px;
        }
        QToolButton#KeyBaseSegment:hover, QToolButton#OffsetBaseSegment:hover, QToolButton#EntryViewModeSegment:hover {
            color: @text;
        }
        QToolButton#KeyBaseSegment:checked, QToolButton#OffsetBaseSegment:checked, QToolButton#EntryViewModeSegment:checked {
            background: @accentSoft;
            color: @text;
            font-weight: 650;
        }
        QLineEdit:focus, QComboBox:focus {
            border-color: @accent;
        }
        QListView, QTreeView, QTableWidget, QPlainTextEdit, QScrollArea, HexPreviewWidget#HexPreview {
            background: @surface;
            alternate-background-color: @surfaceAlt;
            color: @text;
            border: 1px solid @border;
            selection-background-color: @accent;
            selection-color: white;
        }
        HexPreviewWidget#HexPreview {
            border-radius: 4px;
        }
        QScrollBar:vertical {
            background: @scrollbarTrack;
            border: 0;
            width: 12px;
            margin: 3px 2px 3px 2px;
            border-radius: 6px;
        }
        QScrollBar::handle:vertical {
            background: @scrollbarHandle;
            border-radius: 5px;
            min-height: 28px;
        }
        QScrollBar::handle:vertical:hover, QScrollBar::handle:vertical:pressed {
            background: @scrollbarHandleHover;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
            border: 0;
            background: transparent;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar:horizontal {
            background: @scrollbarTrack;
            border: 0;
            height: 12px;
            margin: 2px 3px 2px 3px;
            border-radius: 6px;
        }
        QScrollBar::handle:horizontal {
            background: @scrollbarHandle;
            border-radius: 5px;
            min-width: 28px;
        }
        QScrollBar::handle:horizontal:hover, QScrollBar::handle:horizontal:pressed {
            background: @scrollbarHandleHover;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0;
            border: 0;
            background: transparent;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: transparent;
        }
        QTableWidget {
            gridline-color: @borderSoft;
        }
        QListView#LoadedFileView, QTreeView#EntryTree, QTreeView#NestedEntryTree {
            border-radius: 4px;
            padding: 3px;
            outline: 0;
        }
        QPlainTextEdit {
            color: @mono;
        }
        QHeaderView::section {
            background: @surfaceAlt;
            color: @text;
            border: 0;
            border-right: 1px solid @borderSoft;
            border-bottom: 1px solid @border;
            padding: 4px 6px;
            font-weight: 500;
        }
        QTreeView::item, QListView::item {
            min-height: 28px;
            padding: 4px 7px;
            border: 1px solid transparent;
        }
        QTreeView::item:hover, QListView::item:hover {
            background: @accentSoft;
        }
        QTreeView::item:selected, QListView::item:selected {
            background: @accent;
            color: white;
            border-color: @accent;
        }
        QTreeView::item:selected:!active, QListView::item:selected:!active {
            background: @accentSoft;
            color: @text;
            border-color: @accent;
        }
        QTreeView::branch {
            background: transparent;
        }
        QTreeView::branch:selected, QTreeView::branch:hover {
            background: transparent;
        }
        QListWidget#LoopList {
            background: @surface;
            alternate-background-color: @surface;
            color: @text;
            border: 1px solid @borderSoft;
            border-radius: 4px;
            padding: 2px;
            selection-background-color: @accentSoft;
            selection-color: @text;
        }
        QListWidget#LoopList::item {
            border: 1px solid transparent;
            border-radius: 3px;
            padding: 3px 8px;
            margin: 1px;
        }
        QListWidget#LoopList::item:hover {
            background: @accentSoft;
            color: @text;
        }
        QListWidget#LoopList::item:selected {
            background: @accentSoft;
            color: @text;
            border-left: 3px solid @accent;
        }
        QToolButton {
            background: @surfaceAlt;
            border: 1px solid @border;
            border-radius: 4px;
            padding: 3px;
        }
        QToolButton:hover, QToolButton:focus {
            border-color: @accent;
            background: @accentSoft;
        }
        QToolButton:checked {
            border-color: @accent;
            background: @accentSoft;
        }
        QToolButton#EdgeButton, QToolButton#RailActionButton {
            background: @surface;
            border: 1px solid @border;
            border-radius: 4px;
            padding: 4px;
        }
        QToolButton#EdgeButton:hover, QToolButton#RailActionButton:hover {
            background: @accentSoft;
            border-color: @accent;
        }
        QToolButton#EdgeButton:checked {
            background: @accentSoft;
            border-color: @accent;
        }
        QToolButton#ActionButton {
            background: @surface;
            border: 1px solid @border;
            border-radius: 4px;
            padding: 5px 9px;
        }
        QToolButton#ActionButton:hover, QToolButton#ActionButton:focus {
            background: @accentSoft;
            border-color: @accent;
        }
        QPushButton {
            background: @surface;
            color: @text;
            border: 1px solid @border;
            border-radius: 4px;
            padding: 6px 11px;
            min-height: 18px;
        }
        QPushButton:hover, QPushButton:focus {
            background: @accentSoft;
            border-color: @accent;
        }
        QPushButton:pressed {
            background: @raised;
        }
        QPushButton:default {
            background: @accent;
            border-color: @accent;
            color: white;
        }
        QPushButton:disabled, QToolButton:disabled, QComboBox:disabled, QLineEdit:disabled {
            background: @surfaceAlt;
            color: @muted;
            border-color: @borderSoft;
        }
        QSpinBox, QDoubleSpinBox, QDateTimeEdit {
            background: @surface;
            color: @text;
            border: 1px solid @border;
            border-radius: 4px;
            padding: 4px 6px;
        }
        QSpinBox:focus, QDoubleSpinBox:focus, QDateTimeEdit:focus {
            border-color: @accent;
        }
        QGroupBox {
            color: @text;
            border: 1px solid @borderSoft;
            border-radius: 4px;
            margin-top: 9px;
            padding-top: 8px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px;
        }
        QToolTip {
            background: @surface;
            color: @text;
            border: 1px solid @border;
            padding: 4px 6px;
        }
        QSlider::groove:horizontal {
            height: 5px;
            background: @borderSoft;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            width: 14px;
            margin: -5px 0;
            border-radius: 7px;
            background: @accent;
        }
        QProgressBar {
            border: 1px solid @border;
            border-radius: 4px;
            background: @surface;
            text-align: center;
        }
        QProgressBar::chunk {
            background: @accent;
            border-radius: 3px;
        }
    )");
    css.replace(QStringLiteral("@window"), window);
    css.replace(QStringLiteral("@chrome"), dark ? QStringLiteral("#1f2528") : QStringLiteral("#e8efeb"));
    css.replace(QStringLiteral("@rail"), dark ? QStringLiteral("#202528") : QStringLiteral("#edf2ef"));
    css.replace(QStringLiteral("@surfaceAlt"), surfaceAlt);
    css.replace(QStringLiteral("@surface"), surface);
    css.replace(QStringLiteral("@borderSoft"), borderSoft);
    css.replace(QStringLiteral("@border"), border);
    css.replace(QStringLiteral("@text"), text);
    css.replace(QStringLiteral("@muted"), muted);
    css.replace(QStringLiteral("@accentSoft"), accentSoft);
    css.replace(QStringLiteral("@accent"), accent);
    css.replace(QStringLiteral("@raised"), raised);
    css.replace(QStringLiteral("@scrollbarTrack"), scrollbarTrack);
    css.replace(QStringLiteral("@scrollbarHandleHover"), scrollbarHandleHover);
    css.replace(QStringLiteral("@scrollbarHandle"), scrollbarHandle);
    css.replace(QStringLiteral("@mono"), mono);
    css.replace(QStringLiteral("@dropOverlayRgb"), dark ? QStringLiteral("27, 31, 33") : QStringLiteral("248, 250, 247"));
    return css;
}


QIcon make_sidebar_icon(bool panel_on_left) {
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    const auto palette = QApplication::palette();
    const QColor stroke = palette.color(QPalette::Text);
    QColor accent = palette.color(QPalette::Highlight);
    QColor fill = accent;
    fill.setAlpha(95);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(stroke, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(2.5, 2.5, 13.0, 13.0), 1.5, 1.5);

    const auto divider_x = panel_on_left ? 6.0 : 11.0;
    painter.setPen(QPen(accent, 1.6));
    painter.drawLine(QPointF(divider_x, 3.5), QPointF(divider_x, 14.5));
    painter.fillRect(
        panel_on_left ? QRectF(3.5, 3.5, divider_x - 3.5, 11.0)
                      : QRectF(divider_x, 3.5, 14.5 - divider_x, 11.0),
        fill
    );
    return QIcon(pixmap);
}

QIcon make_action_icon(ActionGlyph glyph) {
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);

    const auto palette = QApplication::palette();
    const QColor text = palette.color(QPalette::Text);
    QColor muted = palette.color(QPalette::PlaceholderText);
    if (!muted.isValid()) {
        muted = palette.color(QPalette::Mid);
    }
    QColor accent = palette.color(QPalette::Highlight);
    QColor accent_fill = accent;
    accent_fill.setAlpha(55);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(text, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch (glyph) {
    case ActionGlyph::Extract:
        painter.drawRoundedRect(QRectF(4.5, 12.0, 13.0, 5.5), 1.4, 1.4);
        painter.setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(11.0, 4.5), QPointF(11.0, 13.0));
        painter.drawLine(QPointF(7.5, 9.8), QPointF(11.0, 13.0));
        painter.drawLine(QPointF(14.5, 9.8), QPointF(11.0, 13.0));
        painter.setBrush(accent_fill);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(6.5, 14.0, 9.0, 1.6), 0.8, 0.8);
        break;
    case ActionGlyph::RawExtract:
        painter.drawRoundedRect(QRectF(4.5, 4.0, 12.0, 14.0), 1.6, 1.6);
        painter.setPen(QPen(muted, 1.2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(7.0, 8.0), QPointF(14.0, 8.0));
        painter.drawLine(QPointF(7.0, 11.0), QPointF(14.0, 11.0));
        painter.drawLine(QPointF(7.0, 14.0), QPointF(11.5, 14.0));
        painter.setPen(QPen(accent, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(15.0, 5.0), QPointF(18.0, 8.0));
        painter.drawLine(QPointF(18.0, 8.0), QPointF(15.0, 11.0));
        break;
    case ActionGlyph::RecoverKey:
        painter.setPen(QPen(text, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawEllipse(QPointF(7.5, 9.0), 3.5, 3.5);
        painter.drawLine(QPointF(10.5, 11.5), QPointF(17.5, 18.0));
        painter.drawLine(QPointF(14.4, 15.1), QPointF(16.6, 12.9));
        painter.drawLine(QPointF(16.1, 16.7), QPointF(18.2, 14.6));
        painter.setPen(QPen(accent, 1.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(QRectF(3.0, 3.0, 9.0, 9.0), 35 * 16, 100 * 16);
        break;
    case ActionGlyph::Clear:
        painter.setPen(QPen(text, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(5.8, 8.0), QPointF(16.2, 8.0));
        painter.drawRoundedRect(QRectF(6.5, 8.5, 9.0, 9.8), 1.4, 1.4);
        painter.drawLine(QPointF(8.5, 5.9), QPointF(13.5, 5.9));
        painter.drawLine(QPointF(9.6, 4.8), QPointF(12.4, 4.8));
        painter.setPen(QPen(accent, 1.2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(9.3, 11.0), QPointF(9.3, 16.0));
        painter.drawLine(QPointF(12.7, 11.0), QPointF(12.7, 16.0));
        break;
    case ActionGlyph::MuxPreview:
        painter.setPen(QPen(text, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawRoundedRect(QRectF(4.5, 5.0, 13.0, 11.5), 2.0, 2.0);
        painter.setBrush(accent_fill);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(8.0, 10.8), 2.0, 2.0);
        painter.drawEllipse(QPointF(14.0, 10.8), 2.0, 2.0);
        painter.setPen(QPen(accent, 1.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(10.0, 10.8), QPointF(12.0, 10.8));
        painter.drawLine(QPointF(17.5, 9.0), QPointF(19.5, 7.5));
        painter.drawLine(QPointF(17.5, 12.6), QPointF(19.5, 14.1));
        break;
    }

    return QIcon(pixmap);
}

QToolButton* make_panel_button(const QIcon& icon, const QString& tooltip, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("EdgeButton"));
    button->setIcon(icon);
    button->setIconSize(QSize(18, 18));
    button->setToolTip(tooltip);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFixedSize(24, 34);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

QToolButton* make_rail_action_button(const QIcon& icon, const QString& tooltip, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("RailActionButton"));
    button->setIcon(icon);
    button->setIconSize(QSize(19, 19));
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFixedSize(28, 30);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}


QString app_title() {
    return QStringLiteral("CriStudio %1 (rev %2)")
        .arg(QStringLiteral(CRISTUDIO_VERSION), QStringLiteral(CRISTUDIO_SOURCE_REVISION));
}



void fade_widget_in(QWidget* widget, int duration_ms) {
    if (widget == nullptr || widget->isVisible()) {
        if (widget != nullptr) {
            widget->show();
        }
        return;
    }

    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect());
    if (effect == nullptr) {
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
    }
    effect->setOpacity(0.0);
    widget->show();

    auto* animation = new QPropertyAnimation(effect, "opacity", widget);
    animation->setDuration(duration_ms);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(animation, &QPropertyAnimation::finished, widget, [effect] {
        effect->setOpacity(1.0);
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

std::optional<cricodecs::KeyRecoveryMode> choose_key_recovery_mode(
    QWidget* parent,
    QString title,
    size_t source_count)
{
    if (source_count <= 1u) return cricodecs::KeyRecoveryMode::SharedBaseKey;

    QDialog dialog(parent);
    dialog.setWindowTitle(std::move(title));
    dialog.setMinimumWidth(620);
    dialog.resize(680, dialog.sizeHint().height());
    dialog.setSizeGripEnabled(true);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    auto* heading = new QLabel(
        QCoreApplication::translate("MainWindow.UiHelpers", "How should %1 selected files be interpreted?")
            .arg(QLocale().toString(static_cast<qulonglong>(source_count))),
        &dialog);
    heading->setObjectName(QStringLiteral("RecoveryNotificationTitle"));
    heading->setWordWrap(true);
    layout->addWidget(heading);

    auto* independent = new QRadioButton(QCoreApplication::translate("MainWindow.UiHelpers", "Recover each file independently"), &dialog);
    independent->setChecked(true);
    layout->addWidget(independent);
    auto* independent_help = dim_label(
        QCoreApplication::translate("MainWindow.UiHelpers", "Use this when the selection may contain unrelated banks or containers with different keys."),
        &dialog);
    independent_help->setWordWrap(true);
    independent_help->setContentsMargins(24, 0, 0, 4);
    layout->addWidget(independent_help);

    auto* shared = new QRadioButton(QCoreApplication::translate("MainWindow.UiHelpers", "Treat all files as one shared base-key set"), &dialog);
    layout->addWidget(shared);
    auto* shared_help = dim_label(
        QCoreApplication::translate("MainWindow.UiHelpers", "Combine evidence across files. Choose this only when they are expected to share one base key; AWB subkeys are handled automatically."),
        &dialog);
    shared_help->setWordWrap(true);
    shared_help->setContentsMargins(24, 0, 0, 4);
    layout->addWidget(shared_help);

    auto* note = dim_label(
        QCoreApplication::translate("MainWindow.UiHelpers", "Files without a supported encrypted stream are skipped and reported."),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = dialog_buttons(
        dialog, QCoreApplication::translate("MainWindow.UiHelpers", "Recover"));
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return shared->isChecked()
        ? cricodecs::KeyRecoveryMode::SharedBaseKey
        : cricodecs::KeyRecoveryMode::Independent;
}

bool show_key_recovery_candidate(float score, float best_score) noexcept {
    constexpr float RelativeFloor = 0.1f;
    return score > 0.0f && (best_score <= 0.0f || score >= best_score * RelativeFloor);
}

bool KeyRecoveryProgressThrottle::ready(size_t completed, size_t total) {
    constexpr size_t MinimumStep = 16;
    constexpr qint64 MinimumIntervalMs = 120;
    if (!m_timer.isValid()) {
        m_timer.start();
        m_last_completed = completed;
        return true;
    }
    if (completed >= total || completed - m_last_completed >= MinimumStep ||
        m_timer.elapsed() >= MinimumIntervalMs) {
        m_last_completed = completed;
        m_timer.restart();
        return true;
    }
    return false;
}

std::vector<KeyRecoveryGroup> group_key_recovery_candidates(
    std::span<const KeyRecoveryCandidate> candidates,
    size_t max_groups) {
    constexpr size_t MaxRetainedFilesPerKey = 100;
    struct Accumulator {
        uint64_t identity = 0;
        QString key;
        double score_sum = 0.0;
        QStringList files;
        size_t count = 0;
        size_t omitted_files = 0;
        size_t recommended_count = 0;
    };

    QHash<QString, float> best_by_file;
    best_by_file.reserve(static_cast<qsizetype>(candidates.size()));
    for (const auto& candidate : candidates) {
        if (candidate.score <= 0.0f) continue;
        const auto current = best_by_file.constFind(candidate.file);
        if (current == best_by_file.cend() || candidate.score > current.value()) {
            best_by_file.insert(candidate.file, candidate.score);
        }
    }

    std::vector<Accumulator> accumulated;
    std::unordered_map<uint64_t, size_t> group_indices;
    accumulated.reserve(std::min(candidates.size(), cricodecs::MaxKeyRecoveryCandidates));
    group_indices.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const auto best = best_by_file.constFind(candidate.file);
        if (best == best_by_file.cend() || !show_key_recovery_candidate(candidate.score, best.value())) {
            continue;
        }
        const auto [position, inserted] = group_indices.try_emplace(candidate.identity, accumulated.size());
        if (inserted) {
            accumulated.push_back(Accumulator{
                .identity = candidate.identity,
                .key = candidate.key,
            });
        }
        auto& group = accumulated[position->second];
        if (group.files.contains(candidate.file)) continue;
        group.score_sum += candidate.score;
        ++group.count;
        if (candidate.recommended) ++group.recommended_count;
        if (group.files.size() < static_cast<qsizetype>(MaxRetainedFilesPerKey)) {
            group.files.push_back(candidate.file);
        } else {
            ++group.omitted_files;
        }
    }
    if (accumulated.empty()) {
        return {};
    }

    const auto better_group = [](const auto& left, const auto& right) {
        if (left.recommended_count != right.recommended_count) {
            return left.recommended_count > right.recommended_count;
        }
        const auto left_score = left.score_sum / static_cast<double>(left.count);
        const auto right_score = right.score_sum / static_cast<double>(right.count);
        if (left_score != right_score) {
            return left_score > right_score;
        }
        if (left.count != right.count) {
            return left.count > right.count;
        }
        const auto left_file = left.files.isEmpty() ? QString{} : left.files.front();
        const auto right_file = right.files.isEmpty() ? QString{} : right.files.front();
        if (left_file != right_file) {
            return left_file < right_file;
        }
        return left.identity < right.identity;
    };
    if (max_groups != 0 && accumulated.size() > max_groups) {
        std::partial_sort(
            accumulated.begin(),
            accumulated.begin() + static_cast<std::ptrdiff_t>(max_groups),
            accumulated.end(),
            better_group);
        accumulated.resize(max_groups);
    } else {
        std::ranges::sort(accumulated, better_group);
    }

    std::vector<KeyRecoveryGroup> groups;
    groups.reserve(accumulated.size());
    for (const auto& group : accumulated) {
        groups.push_back(KeyRecoveryGroup{
            .identity = group.identity,
            .key = group.key,
            .mean_score = static_cast<float>(group.score_sum / static_cast<double>(group.count)),
            .count = group.count,
            .files = group.files,
            .omitted_files = group.omitted_files,
            .recommended_count = group.recommended_count,
        });
    }
    return groups;
}

namespace {

constexpr int RecoveryScoreRole = Qt::UserRole;
constexpr int RecoveryCountRole = Qt::UserRole + 1;
constexpr int RecoveryFilesRole = Qt::UserRole + 2;
constexpr int RecoveryOmittedFilesRole = Qt::UserRole + 3;
constexpr int RecoveryKeyRole = Qt::UserRole + 4;
constexpr int RecoveryRecommendedRole = Qt::UserRole + 5;
constexpr qsizetype MaxVisibleRecoveryGroups = 250;
constexpr qsizetype MaxInterimRecoveryGroups = static_cast<qsizetype>(MaxInterimKeyRecoveryGroups);

[[nodiscard]] QString file_preview(const KeyRecoveryGroup& group) {
    constexpr qsizetype PreviewCount = 2;
    QStringList shown;
    const auto count = std::min(PreviewCount, group.files.size());
    shown.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        shown.push_back(group.files[index]);
    }
    const size_t remaining = group.count > static_cast<size_t>(count)
        ? group.count - static_cast<size_t>(count)
        : 0u;
    auto result = shown.join(QStringLiteral(", "));
    if (remaining != 0u) {
        result += QCoreApplication::translate("MainWindow.UiHelpers", "  +%1 more").arg(static_cast<qulonglong>(remaining));
    }
    return result;
}

class RecoveryTreeItem final : public QTreeWidgetItem {
public:
    explicit RecoveryTreeItem(QTreeWidget* tree) : QTreeWidgetItem(tree) {}

    bool operator<(const QTreeWidgetItem& other) const override {
        const auto left_recommended = data(0, RecoveryRecommendedRole).toULongLong();
        const auto right_recommended = other.data(0, RecoveryRecommendedRole).toULongLong();
        if (left_recommended != right_recommended) {
            return left_recommended < right_recommended;
        }
        const int column = treeWidget() == nullptr ? 0 : treeWidget()->sortColumn();
        if (column == 1) {
            const auto left = data(1, RecoveryScoreRole).toDouble();
            const auto right = other.data(1, RecoveryScoreRole).toDouble();
            if (left != right) return left < right;
        } else if (column == 2) {
            const auto left = data(2, RecoveryCountRole).toULongLong();
            const auto right = other.data(2, RecoveryCountRole).toULongLong();
            if (left != right) return left < right;
        } else if (column == 3) {
            const auto comparison = QString::localeAwareCompare(text(3), other.text(3));
            if (comparison != 0) return comparison < 0;
        } else {
            const auto comparison = QString::localeAwareCompare(text(0), other.text(0));
            if (comparison != 0) return comparison < 0;
        }

        const auto left_score = data(1, RecoveryScoreRole).toDouble();
        const auto right_score = other.data(1, RecoveryScoreRole).toDouble();
        if (left_score != right_score) return left_score < right_score;
        const auto left_count = data(2, RecoveryCountRole).toULongLong();
        const auto right_count = other.data(2, RecoveryCountRole).toULongLong();
        if (left_count != right_count) return left_count < right_count;
        const auto files_comparison = QString::localeAwareCompare(text(3), other.text(3));
        if (files_comparison != 0) return files_comparison < 0;
        return QString::localeAwareCompare(text(0), other.text(0)) < 0;
    }
};

void configure_recovery_table(QTreeWidget* table) {
    table->setObjectName(QStringLiteral("RecoveryResultTable"));
    table->setColumnCount(4);
    table->setHeaderLabels({
        QStringLiteral("Key"),
        QCoreApplication::translate("MainWindow.UiHelpers", "Mean score"),
        QStringLiteral("Count"),
        QStringLiteral("Files"),
    });
    table->setRootIsDecorated(true);
    table->setAlternatingRowColors(true);
    table->setUniformRowHeights(true);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSortingEnabled(true);
    table->header()->setSectionsClickable(true);
    table->header()->setSortIndicatorShown(true);
    table->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->headerItem()->setToolTip(1, QCoreApplication::translate("MainWindow.UiHelpers", "Normalized structural score; higher ranks first"));
    table->sortItems(1, Qt::DescendingOrder);
    QObject::connect(table, &QTreeWidget::itemExpanded, table, [](QTreeWidgetItem* item) {
        if (item == nullptr || item->parent() != nullptr || item->childCount() != 0) return;
        const auto files = item->data(3, RecoveryFilesRole).toStringList();
        for (const auto& file : files) {
            auto* child = new QTreeWidgetItem(item);
            child->setText(3, file);
            child->setFlags(child->flags() & ~Qt::ItemIsSelectable);
        }
        const auto omitted = item->data(3, RecoveryOmittedFilesRole).toULongLong();
        if (omitted != 0u) {
            auto* child = new QTreeWidgetItem(item);
            child->setText(3, QCoreApplication::translate("MainWindow.UiHelpers", "%1 additional files omitted to cap memory use")
                .arg(omitted));
            child->setFlags(child->flags() & ~Qt::ItemIsSelectable);
        }
    });
}

void add_recovery_group(QTreeWidget* table, const KeyRecoveryGroup& group) {
    auto* item = new RecoveryTreeItem(table);
    item->setText(0, group.recommended_count == 0u
        ? group.key
        : QStringLiteral("\u2605 %1").arg(group.key));
    item->setText(1, QString::number(group.mean_score, 'f', 6));
    item->setText(2, QString::number(static_cast<qulonglong>(group.count)));
    item->setText(3, file_preview(group));
    item->setData(0, RecoveryKeyRole, group.key);
    item->setData(0, RecoveryRecommendedRole, static_cast<qulonglong>(group.recommended_count));
    item->setData(1, RecoveryScoreRole, group.mean_score);
    item->setData(2, RecoveryCountRole, static_cast<qulonglong>(group.count));
    item->setData(3, RecoveryFilesRole, group.files);
    item->setData(3, RecoveryOmittedFilesRole, static_cast<qulonglong>(group.omitted_files));
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    if (group.recommended_count != 0u) {
        item->setToolTip(0, QCoreApplication::translate(
            "MainWindow.UiHelpers",
            "Recommended by the recovery engine's combined codec evidence for %n source(s).",
            nullptr,
            static_cast<int>(group.recommended_count)));
    }
    if (group.count > 1u) {
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
}

[[nodiscard]] QStringList selected_recovery_keys(const QTreeWidget* table) {
    QStringList keys;
    if (table == nullptr) return keys;
    for (const auto* item : table->selectedItems()) {
        if (item == nullptr || item->parent() != nullptr) continue;
        const auto key = item->data(0, RecoveryKeyRole).toString();
        if (!key.isEmpty() && !keys.contains(key)) keys.push_back(key);
    }
    return keys;
}

void copy_recovery_keys(const QStringList& keys) {
    if (keys.isEmpty()) return;
    if (auto* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(keys.join(QLatin1Char('\n')));
    }
}

class RecoveryProgressDialog final : public QDialog {
public:
    RecoveryProgressDialog(
        QWidget* parent,
        QString title,
        size_t source_count,
        std::function<void()> cancel)
        : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
              Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint)
        , m_cancel(std::move(cancel)) {
        setObjectName(QStringLiteral("KeyRecoveryProgressDialog"));
        setWindowTitle(std::move(title));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::NonModal);
        setSizeGripEnabled(true);
        setMinimumSize(680, 340);
        setModal(false);
        resize(820, 430);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(10);
        m_status = new QLabel(QCoreApplication::translate("MainWindow.UiHelpers", "Preparing key recovery..."), this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
        m_progress = new QProgressBar(this);
        m_progress->setRange(0, static_cast<int>(std::max<size_t>(source_count, 1)));
        layout->addWidget(m_progress);
        m_table = new QTreeWidget(this);
        configure_recovery_table(m_table);
        layout->addWidget(m_table, 1);

        auto* actions = new QHBoxLayout;
        actions->addStretch(1);
        m_copy_selected = new QPushButton(QCoreApplication::translate("MainWindow.UiHelpers", "Copy Selected"), this);
        m_copy_selected->setEnabled(false);
        actions->addWidget(m_copy_selected);
        m_copy_all = new QPushButton(QCoreApplication::translate("MainWindow.UiHelpers", "Copy All"), this);
        m_copy_all->setEnabled(false);
        actions->addWidget(m_copy_all);
        m_cancel_or_close = new QPushButton(QCoreApplication::translate("MainWindow.UiHelpers", "Cancel"), this);
        m_cancel_or_close->setEnabled(static_cast<bool>(m_cancel));
        m_cancel_or_close->setVisible(static_cast<bool>(m_cancel));
        actions->addWidget(m_cancel_or_close);
        layout->addLayout(actions);

        connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
            m_copy_selected->setEnabled(!selected_recovery_keys(m_table).isEmpty());
        });
        connect(m_copy_selected, &QPushButton::clicked, this, [this] {
            copy_recovery_keys(selected_recovery_keys(m_table));
        });
        connect(m_copy_all, &QPushButton::clicked, this, [this] {
            copy_recovery_keys(m_all_keys);
        });
        connect(m_cancel_or_close, &QPushButton::clicked, this, [this] {
            if (m_finished) {
                close();
            } else {
                request_cancel();
            }
        });
        auto* copy_shortcut = new QShortcut(QKeySequence::Copy, m_table);
        connect(copy_shortcut, &QShortcut::activated, this, [this] {
            copy_recovery_keys(selected_recovery_keys(m_table));
        });
    }

    void present() {
        show();
        raise();
        activateWindow();
        if (auto* host = parentWidget(); host != nullptr) {
            const auto center = host->frameGeometry().center();
            move(center - rect().center());
        }
    }

    void update(std::vector<KeyRecoveryGroup> groups, size_t completed, size_t total, QString status, bool finished) {
        QStringList selected = selected_recovery_keys(m_table);
        m_all_keys.clear();
        m_all_keys.reserve(static_cast<qsizetype>(groups.size()));
        for (const auto& group : groups) m_all_keys.push_back(group.key);
        const auto visible_limit = finished ? MaxVisibleRecoveryGroups : MaxInterimRecoveryGroups;
        if (groups.size() > static_cast<size_t>(visible_limit)) {
            status += QCoreApplication::translate("MainWindow.UiHelpers", " Showing the top %1 of %2 key groups.")
                .arg(visible_limit)
                .arg(static_cast<qulonglong>(groups.size()));
        }
        m_status->setText(std::move(status));
        m_progress->setRange(0, static_cast<int>(std::max<size_t>(total, 1)));
        m_progress->setValue(static_cast<int>(completed));
        const auto sort_column = m_table->sortColumn();
        const auto sort_order = m_table->header()->sortIndicatorOrder();
        m_table->setSortingEnabled(false);
        m_table->clear();
        const auto visible = std::min(visible_limit, static_cast<qsizetype>(groups.size()));
        for (qsizetype index = 0; index < visible; ++index) {
            add_recovery_group(m_table, groups[static_cast<size_t>(index)]);
        }
        m_table->setSortingEnabled(true);
        m_table->sortItems(sort_column < 0 ? 1 : sort_column, sort_column < 0 ? Qt::DescendingOrder : sort_order);
        for (int index = 0; index < m_table->topLevelItemCount(); ++index) {
            auto* item = m_table->topLevelItem(index);
            if (selected.contains(item->data(0, RecoveryKeyRole).toString())) item->setSelected(true);
        }
        m_copy_all->setEnabled(!m_all_keys.isEmpty());
        if (finished) {
            m_finished = true;
            m_cancel_or_close->setText(QCoreApplication::translate("MainWindow.UiHelpers", "Close"));
            m_cancel_or_close->setEnabled(true);
            m_progress->setValue(m_progress->maximum());
            present();
        }
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (!m_finished && m_cancel) {
            request_cancel();
        } else if (!m_finished) {
            hide();
            event->ignore();
            return;
        }
        QDialog::closeEvent(event);
    }

private:
    void request_cancel() {
        if (m_finished || m_cancel_requested || !m_cancel) {
            return;
        }
        m_cancel_requested = true;
        m_cancel_or_close->setEnabled(false);
        m_status->setText(QCoreApplication::translate("MainWindow.UiHelpers", "Canceling key recovery..."));
        m_cancel();
    }

    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QTreeWidget* m_table = nullptr;
    QPushButton* m_copy_selected = nullptr;
    QPushButton* m_copy_all = nullptr;
    QPushButton* m_cancel_or_close = nullptr;
    QStringList m_all_keys;
    std::function<void()> m_cancel;
    bool m_finished = false;
    bool m_cancel_requested = false;
};

class RecoveryNotification final : public QDialog {
public:
    RecoveryNotification(
        QWidget* parent,
        QString title,
        QString summary,
        QString details,
        QString copy_text,
        QString copy_button_text,
        std::vector<KeyRecoveryGroup> groups,
        bool error
    )
        : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
              Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint) {
        const bool compact_error = error && groups.empty() && details.isEmpty();
        setObjectName(QStringLiteral("RecoveryNotification"));
        setProperty("severity", error ? QStringLiteral("error") : QStringLiteral("result"));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::NonModal);
        setModal(false);
        setSizeGripEnabled(!compact_error);
        setWindowTitle(title);
        if (compact_error) {
            setWindowFlag(Qt::WindowMinMaxButtonsHint, false);
            setMinimumWidth(420);
            setMaximumWidth(620);
        } else {
            setMinimumSize(groups.empty() ? QSize(460, 220) : QSize(680, 340));
            resize(groups.empty() ? QSize(560, 280) : QSize(860, 480));
        }

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(9);
        if (compact_error) {
            layout->setSizeConstraint(QLayout::SetFixedSize);
        }

        auto* title_label = new QLabel(std::move(title), this);
        title_label->setObjectName(QStringLiteral("RecoveryNotificationTitle"));
        layout->addWidget(title_label);

        auto* summary_label = new QLabel(std::move(summary), this);
        summary_label->setObjectName(QStringLiteral("RecoveryNotificationSummary"));
        summary_label->setWordWrap(true);
        summary_label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        if (compact_error) {
            summary_label->setMinimumWidth(340);
            summary_label->setMaximumWidth(520);
            auto* message_row = new QHBoxLayout;
            message_row->setSpacing(10);
            auto* icon = new QLabel(this);
            icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(22, 22));
            icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
            icon->setAccessibleName(QCoreApplication::translate("MainWindow.UiHelpers", "Recovery warning"));
            message_row->addWidget(icon, 0, Qt::AlignTop);
            message_row->addWidget(summary_label, 1);
            layout->addLayout(message_row);
        } else {
            layout->addWidget(summary_label);
        }

        QTreeWidget* table = nullptr;
        if (!groups.empty()) {
            table = new QTreeWidget(this);
            configure_recovery_table(table);
            const auto visible = std::min(MaxVisibleRecoveryGroups, static_cast<qsizetype>(groups.size()));
            for (qsizetype index = 0; index < visible; ++index) {
                add_recovery_group(table, groups[static_cast<size_t>(index)]);
            }
            table->sortItems(1, Qt::DescendingOrder);
            layout->addWidget(table, 1);
        }

        QPlainTextEdit* details_view = nullptr;
        if (!details.isEmpty()) {
            details_view = new QPlainTextEdit(std::move(details), this);
            details_view->setObjectName(QStringLiteral("RecoveryNotificationDetails"));
            details_view->setReadOnly(true);
            details_view->setMaximumBlockCount(200);
            details_view->setFixedHeight(128);
            details_view->hide();
            layout->addWidget(details_view);
        }

        auto* actions = new QHBoxLayout;
        actions->setSpacing(7);
        actions->addStretch(1);
        if (details_view != nullptr) {
            auto* details_button = new QPushButton(QCoreApplication::translate("MainWindow.UiHelpers", "Details"), this);
            details_button->setCheckable(true);
            QObject::connect(details_button, &QPushButton::toggled, this, [details_button, details_view](bool visible) {
                details_view->setVisible(visible);
                details_button->setText(visible ? QCoreApplication::translate("MainWindow.UiHelpers", "Hide details") : QCoreApplication::translate("MainWindow.UiHelpers", "Details"));
            });
            actions->addWidget(details_button);
        }
        QPushButton* copy_selected = nullptr;
        if (table != nullptr) {
            copy_selected = new QPushButton(QCoreApplication::translate("MainWindow.UiHelpers", "Copy Selected"), this);
            copy_selected->setEnabled(false);
            QObject::connect(table->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [table, copy_selected] {
                    copy_selected->setEnabled(!selected_recovery_keys(table).isEmpty());
                });
            QObject::connect(copy_selected, &QPushButton::clicked, this, [table, copy_selected] {
                copy_recovery_keys(selected_recovery_keys(table));
                copy_selected->setText(QCoreApplication::translate("MainWindow.UiHelpers", "Copied"));
            });
            auto* copy_shortcut = new QShortcut(QKeySequence::Copy, table);
            QObject::connect(copy_shortcut, &QShortcut::activated, this, [table] {
                copy_recovery_keys(selected_recovery_keys(table));
            });
            actions->addWidget(copy_selected);
        }
        if (!copy_text.isEmpty()) {
            auto* copy_button = new QPushButton(std::move(copy_button_text), this);
            QObject::connect(copy_button, &QPushButton::clicked, this, [copy_button, copy_text = std::move(copy_text)] {
                if (auto* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
                    clipboard->setText(copy_text);
                    copy_button->setText(QCoreApplication::translate("MainWindow.UiHelpers", "Copied"));
                }
            });
            actions->addWidget(copy_button);
        }
        layout->addLayout(actions);

    }

    void present() {
        show();
        raise();
        activateWindow();
        QTimer::singleShot(0, this, [this] {
            raise();
            if (auto* host = parentWidget(); host != nullptr) {
                const auto center = host->frameGeometry().center();
                move(center - rect().center());
            }
        });
    }
};

void show_recovery_message(
    QWidget* parent,
    QString title,
    QString summary,
    QString details,
    QString copy_text,
    QString copy_button_text,
    std::vector<KeyRecoveryGroup> groups,
    bool error
) {
    if (parent == nullptr) {
        return;
    }
    for (auto* current : parent->findChildren<QWidget*>(
             QStringLiteral("RecoveryNotification"), Qt::FindDirectChildrenOnly)) {
        current->close();
    }
    auto* notification = new RecoveryNotification(
        parent,
        std::move(title),
        std::move(summary),
        std::move(details),
        std::move(copy_text),
        std::move(copy_button_text),
        std::move(groups),
        error
    );
    notification->present();
}

} // namespace

void begin_key_recovery_progress(
    QWidget* parent,
    QString title,
    size_t source_count,
    std::function<void()> cancel) {
    if (parent == nullptr) return;
    for (auto* current : parent->findChildren<QDialog*>(
             QStringLiteral("KeyRecoveryProgressDialog"), Qt::FindDirectChildrenOnly)) {
        current->close();
    }
    auto* dialog = new RecoveryProgressDialog(
        parent, std::move(title), source_count, std::move(cancel));
    dialog->present();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void update_key_recovery_progress(
    QWidget* parent,
    std::vector<KeyRecoveryGroup> groups,
    size_t completed,
    size_t total,
    QString status,
    bool finished) {
    if (parent == nullptr) return;
    auto* dialog = dynamic_cast<RecoveryProgressDialog*>(parent->findChild<QDialog*>(
        QStringLiteral("KeyRecoveryProgressDialog"), Qt::FindDirectChildrenOnly));
    if (dialog != nullptr) {
        dialog->update(std::move(groups), completed, total, std::move(status), finished);
    }
}

void show_key_recovery_result(
    QWidget* parent,
    QString title,
    QString summary,
    QString details,
    QString copy_text,
    QString copy_button_text,
    std::vector<KeyRecoveryGroup> groups
) {
    show_recovery_message(
        parent,
        std::move(title),
        std::move(summary),
        std::move(details),
        std::move(copy_text),
        std::move(copy_button_text),
        std::move(groups),
        false
    );
}

void show_key_recovery_error(QWidget* parent, QString title, QString error) {
    show_recovery_message(
        parent,
        std::move(title),
        std::move(error),
        {},
        {},
        {},
        {},
        true
    );
}



} // namespace cristudio
