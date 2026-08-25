#include "shared/i18n.hpp"
#include "../main_window.hpp"

#include "key_panel.hpp"
#include "ui_helpers.hpp"
#include "../path_text.hpp"
#include "adx_crypto.hpp"

#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>

namespace cristudio {
namespace {

std::string qt_to_utf8(const QString& text) {
    const auto utf8 = text.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

QSettings key_profile_settings() {
    return QSettings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
}

QString key_profile_group(const QString& name) {
    return QStringLiteral("keyProfiles/") + name;
}

void write_key_profile(QSettings& settings, const QString& name, const DecryptionKeys& keys) {
    settings.beginGroup(key_profile_group(name));
    settings.setValue(QStringLiteral("hasCriKey"), keys.has_cri_key);
    settings.setValue(QStringLiteral("criKey"), QString::number(keys.cri_key));
    settings.setValue(QStringLiteral("hcaSubkey"), QString::number(keys.hca_subkey));
    settings.setValue(QStringLiteral("adxMode"), static_cast<int>(keys.adx_mode));
    settings.setValue(QStringLiteral("adxType8Key"), utf8_to_qstring(keys.adx_type8_key));
    settings.setValue(QStringLiteral("adxType9Key"), QString::number(keys.adx_type9_key));
    settings.setValue(QStringLiteral("adxSubkey"), QString::number(keys.adx_subkey));
    settings.setValue(QStringLiteral("ahxStart"), QString::number(keys.ahx_start));
    settings.setValue(QStringLiteral("ahxMult"), QString::number(keys.ahx_mult));
    settings.setValue(QStringLiteral("ahxAdd"), QString::number(keys.ahx_add));
    settings.endGroup();
}

bool read_key_profile(QSettings& settings, const QString& name, DecryptionKeys& keys) {
    settings.beginGroup(key_profile_group(name));
    if (settings.childKeys().isEmpty()) {
        settings.endGroup();
        return false;
    }
    keys = {};
    keys.has_cri_key = settings.value(QStringLiteral("hasCriKey"), false).toBool();
    keys.cri_key = settings.value(QStringLiteral("criKey"), QStringLiteral("0")).toString().toULongLong();
    keys.hca_subkey = static_cast<uint16_t>(settings.value(QStringLiteral("hcaSubkey"), QStringLiteral("0")).toString().toUInt());
    const auto adx_mode = settings.value(QStringLiteral("adxMode"), static_cast<int>(DecryptionKeys::AdxMode::None)).toInt();
    if (adx_mode >= static_cast<int>(DecryptionKeys::AdxMode::None) &&
        adx_mode <= static_cast<int>(DecryptionKeys::AdxMode::AhxTriplet)) {
        keys.adx_mode = static_cast<DecryptionKeys::AdxMode>(adx_mode);
    }
    keys.adx_type8_key = qt_to_utf8(settings.value(QStringLiteral("adxType8Key")).toString());
    keys.adx_type9_key = settings.value(QStringLiteral("adxType9Key"), QStringLiteral("0")).toString().toULongLong();
    keys.adx_subkey = static_cast<uint16_t>(settings.value(QStringLiteral("adxSubkey"), QStringLiteral("0")).toString().toUInt());
    keys.ahx_start = static_cast<uint16_t>(settings.value(QStringLiteral("ahxStart"), QStringLiteral("0")).toString().toUInt());
    keys.ahx_mult = static_cast<uint16_t>(settings.value(QStringLiteral("ahxMult"), QStringLiteral("0")).toString().toUInt());
    keys.ahx_add = static_cast<uint16_t>(settings.value(QStringLiteral("ahxAdd"), QStringLiteral("0")).toString().toUInt());
    settings.endGroup();
    return true;
}

QString lower_text(std::string_view text) {
    auto value = utf8_to_qstring(std::string(text)).toLower();
    return value;
}

const InfoRow* find_info_row(const std::vector<InfoRow>& rows, std::string_view name) {
    const auto found = std::ranges::find_if(rows, [name](const InfoRow& row) {
        return info_row_id(row) == name;
    });
    return found == rows.end() ? nullptr : std::addressof(*found);
}

bool info_is_yes(const std::vector<InfoRow>& rows, std::string_view name) {
    const auto* row = find_info_row(rows, name);
    return row != nullptr && lower_text(info_row_value_id(*row)).trimmed() == QStringLiteral("yes");
}

bool info_contains(const std::vector<InfoRow>& rows, std::string_view name, QStringView needle) {
    const auto* row = find_info_row(rows, name);
    return row != nullptr && lower_text(info_row_value_id(*row)).contains(needle);
}

int info_int_value(const std::vector<InfoRow>& rows, std::string_view name, int fallback = 0) {
    const auto* row = find_info_row(rows, name);
    if (row == nullptr) {
        return fallback;
    }

    bool ok = false;
    const auto value = utf8_to_qstring(row->value).trimmed().toInt(&ok, 0);
    return ok ? value : fallback;
}

bool text_suggests_key_required(const QString& text) {
    return text.contains(QStringLiteral("encrypted")) ||
           text.contains(QStringLiteral("scrambled")) ||
           text.contains(QStringLiteral("inaccessible")) ||
           text.contains(QCoreApplication::translate("MainWindow.KeyPanel", "key required")) ||
           text.contains(QCoreApplication::translate("MainWindow.KeyPanel", "needs key")) ||
           text.contains(QCoreApplication::translate("MainWindow.KeyPanel", "needs a decryption key")) ||
           text.contains(QCoreApplication::translate("MainWindow.KeyPanel", "required for toc"));
}

QString compact_key_text(QString text) {
    text = text.trimmed();
    text.remove(QLatin1Char('_'));
    text.remove(QLatin1Char(' '));
    return text;
}

QString key64_display(uint64_t value) {
    return QStringLiteral("0x%1").arg(QString::number(value, 16).toUpper(), 16, QLatin1Char('0'));
}

QString key16_display(uint16_t value) {
    return QStringLiteral("%1").arg(QString::number(value, 16).toUpper(), 4, QLatin1Char('0'));
}

QString adx_key_display(const DecryptionKeys& keys) {
    switch (keys.adx_mode) {
    case DecryptionKeys::AdxMode::Type8String:
        return QStringLiteral("type8:") + utf8_to_qstring(keys.adx_type8_key);
    case DecryptionKeys::AdxMode::Type9Number:
        return key64_display(keys.adx_type9_key);
    case DecryptionKeys::AdxMode::AhxTriplet:
        return QStringLiteral("%1,%2,%3").arg(keys.ahx_start).arg(keys.ahx_mult).arg(keys.ahx_add);
    case DecryptionKeys::AdxMode::None:
        return {};
    }
    return {};
}

bool is_adx_key_numeric_candidate(const QString& text) {
    const auto compact = compact_key_text(text);
    if (compact.isEmpty()) {
        return false;
    }
    auto body = compact;
    if (body.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        body = body.mid(2);
    }
    return std::ranges::all_of(body, [](QChar ch) {
        return ch.isDigit() ||
               (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) ||
               (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
    });
}

QComboBox* make_key_base_combo(QWidget* parent);
QWidget* make_key_base_selector(QComboBox*& base, QLineEdit* input, QWidget* parent);
void sync_key_base_selector(QComboBox* base);
void set_key_base_selector_visible(QComboBox* base, bool visible);
void set_key_base_selector_enabled(QComboBox* base, bool enabled);

QComboBox* make_key_base_combo(QWidget* parent) {
    auto* combo = new QComboBox(parent);
    combo->setObjectName(QStringLiteral("KeyBaseCombo"));
    combo->addItem(QCoreApplication::translate("MainWindow.KeyPanel", "hex"), 16);
    combo->addItem(QCoreApplication::translate("MainWindow.KeyPanel", "dec"), 10);
    combo->setCurrentIndex(0);
    combo->setToolTip(QCoreApplication::translate("MainWindow.KeyPanel", "Choose how the numeric key text is parsed."));
    combo->setAccessibleName(QCoreApplication::translate("MainWindow.KeyPanel", "Numeric key base"));
    combo->hide();
    return combo;
}

bool convert_key_text_base(QLineEdit* input, int from_base, int to_base) {
    if (input == nullptr || from_base == to_base) {
        return true;
    }
    auto text = input->text().trimmed();
    if (text.isEmpty()) {
        return true;
    }

    text.remove(QLatin1Char('_'));
    auto body = text;
    if (body.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        if (from_base != 16) {
            return false;
        }
        body = body.mid(2);
    }

    if (from_base == 16) {
        body.remove(QLatin1Char(' '));
    } else if (body.contains(QLatin1Char(' '))) {
        return false;
    }

    bool ok = false;
    const auto value = body.toULongLong(&ok, from_base);
    if (!ok) {
        return false;
    }

    const QSignalBlocker blocker(input);
    input->setText(to_base == 16
        ? QString::number(value, 16).toUpper()
        : QString::number(value, 10));
    return true;
}

void sync_key_base_selector(QComboBox* base) {
    if (base == nullptr || base->parentWidget() == nullptr) {
        return;
    }
    const auto active = key_base_value(base);
    const auto buttons = base->parentWidget()->findChildren<QToolButton*>(QStringLiteral("KeyBaseSegment"));
    for (auto* button : buttons) {
        const auto value = button->property("baseValue").toInt();
        const QSignalBlocker blocker(button);
        button->setChecked(value == active);
    }
}

void set_key_base_selector_visible(QComboBox* base, bool visible) {
    if (base != nullptr && base->parentWidget() != nullptr) {
        base->parentWidget()->setVisible(visible);
    }
}

void set_key_base_selector_enabled(QComboBox* base, bool enabled) {
    if (base != nullptr) {
        base->setEnabled(enabled);
        if (base->parentWidget() != nullptr) {
            base->parentWidget()->setEnabled(enabled);
        }
    }
}

QWidget* make_key_base_selector(QComboBox*& base, QLineEdit* input, QWidget* parent) {
    auto* selector = new QWidget(parent);
    selector->setObjectName(QStringLiteral("KeyBaseSelector"));
    auto* layout = new QHBoxLayout(selector);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    base = make_key_base_combo(selector);
    layout->addWidget(base, 0);
    for (const auto& item : {std::pair{QCoreApplication::translate("MainWindow.KeyPanel", "hex"), 16}, std::pair{QCoreApplication::translate("MainWindow.KeyPanel", "dec"), 10}}) {
        auto* button = new QToolButton(selector);
        button->setObjectName(QStringLiteral("KeyBaseSegment"));
        button->setText(item.first);
        button->setProperty("baseValue", item.second);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setToolTip(QCoreApplication::translate("MainWindow.KeyPanel", "Parse this key as %1.").arg(item.first));
        button->setAccessibleName(QCoreApplication::translate("MainWindow.KeyPanel", "Use %1 key input").arg(item.first));
        layout->addWidget(button, 0);
        QObject::connect(button, &QToolButton::clicked, base, [base, input, value = item.second] {
            const auto previous = key_base_value(base);
            if (previous != value) {
                convert_key_text_base(input, previous, value);
            }
            base->setCurrentIndex(value == 10 ? 1 : 0);
            sync_key_base_selector(base);
        });
    }
    QObject::connect(base, qOverload<int>(&QComboBox::currentIndexChanged), base, [base] {
        sync_key_base_selector(base);
    });
    sync_key_base_selector(base);
    return selector;
}

QWidget* make_numeric_key_row(QLineEdit*& input, QComboBox*& base, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    input = new QLineEdit(row);
    auto* base_selector = make_key_base_selector(base, input, row);
    layout->addWidget(input, 1);
    layout->addWidget(base_selector, 0);
    return row;
}

bool parse_u64_key(QString text, int base, uint64_t& out, QString& error, const QString& label) {
    text = text.trimmed();
    text.remove(QLatin1Char('_'));
    if (text.isEmpty()) {
        out = 0;
        return true;
    }

    auto body = text;
    if (body.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        if (base != 16) {
            error = label + QCoreApplication::translate("MainWindow.KeyPanel", " uses 0x, so choose hex.");
            return false;
        }
        body = body.mid(2);
    }

    if (base == 16) {
        body.remove(QLatin1Char(' '));
    } else {
        if (body.contains(QLatin1Char(' '))) {
            error = label + QCoreApplication::translate("MainWindow.KeyPanel", " has grouped digits; choose hex or remove spaces.");
            return false;
        }
    }

    if (body.isEmpty()) {
        error = label + QCoreApplication::translate("MainWindow.KeyPanel", " is missing key digits.");
        return false;
    }
    if (base == 16 && body.size() > 16) {
        error = label + QCoreApplication::translate("MainWindow.KeyPanel", " must fit in 64 bits.");
        return false;
    }

    bool ok = false;
    const auto value = body.toULongLong(&ok, base);
    if (!ok) {
        error = label + (base == 16
            ? QCoreApplication::translate("MainWindow.KeyPanel", " must contain hex digits.")
            : QCoreApplication::translate("MainWindow.KeyPanel", " must contain decimal digits."));
        return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
}

bool parse_u16_key(QString text, int base, uint16_t& out, QString& error, const QString& label) {
    uint64_t value = 0;
    if (!parse_u64_key(std::move(text), base, value, error, label)) {
        return false;
    }
    if (value > std::numeric_limits<uint16_t>::max()) {
        error = label + QCoreApplication::translate("MainWindow.KeyPanel", " must fit in 16 bits.");
        return false;
    }
    out = static_cast<uint16_t>(value);
    return true;
}



} // namespace

QWidget* make_key_panel(
    QLabel*& label,
    QLineEdit*& input,
    QComboBox*& base,
    QToolButton*& apply,
    QWidget* parent
) {
    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("KeyPanel"));
    auto* layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(8);
    label = new QLabel(QCoreApplication::translate("MainWindow.KeyPanel", "Key"), panel);
    label->setObjectName(QStringLiteral("DimLabel"));
    input = new QLineEdit(panel);
    input->setClearButtonEnabled(true);
    auto* base_selector = make_key_base_selector(base, input, panel);
    apply = new QToolButton(panel);
    apply->setText(QCoreApplication::translate("MainWindow.KeyPanel", "Apply"));
    apply->setToolButtonStyle(Qt::ToolButtonTextOnly);
    layout->addWidget(label, 0);
    layout->addWidget(input, 1);
    layout->addWidget(base_selector, 0);
    layout->addWidget(apply, 0);
    panel->hide();
    return panel;
}

int key_base_value(const QComboBox* combo) {
    if (combo == nullptr) {
        return 16;
    }
    const auto value = combo->currentData().toInt();
    return value == 10 ? 10 : 16;
}



void MainWindow::show_decryption_keys_panel() {
    build_decryption_keys_window();
    sync_decryption_key_panel_from_state();
    m_decryption_keys_window->show();
    m_decryption_keys_window->raise();
    m_decryption_keys_window->activateWindow();
}

void MainWindow::build_decryption_keys_window() {
    if (m_decryption_keys_window != nullptr) {
        return;
    }

    m_decryption_keys_window = new QDialog(
        this,
        Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    m_decryption_keys_window->setWindowTitle(QCoreApplication::translate("MainWindow.KeyPanel", "Cryptography Keys"));
    m_decryption_keys_window->setObjectName(QStringLiteral("DecryptionKeysWindow"));
    m_decryption_keys_window->setModal(false);
    m_decryption_keys_window->setSizeGripEnabled(true);
    m_decryption_keys_window->setMinimumWidth(440);
    auto* window_layout = new QVBoxLayout(m_decryption_keys_window);
    window_layout->setContentsMargins(0, 0, 0, 0);
    window_layout->setSpacing(0);

    auto* panel = new QWidget(m_decryption_keys_window);
    panel->setObjectName(QStringLiteral("DecryptionKeysPanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setAutoFillBackground(true);
    panel->setMinimumWidth(420);
    window_layout->addWidget(panel);
    auto* root = new QVBoxLayout(panel);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* profile_group = new QGroupBox(QCoreApplication::translate("MainWindow.KeyPanel", "Profiles"), panel);
    auto* profile_layout = new QHBoxLayout(profile_group);
    profile_layout->setContentsMargins(10, 8, 10, 8);
    profile_layout->setSpacing(6);
    m_key_profile_combo = new QComboBox(profile_group);
    m_key_profile_combo->setEditable(false);
    m_key_profile_combo->setToolTip(QCoreApplication::translate("MainWindow.KeyPanel", "Saved cryptography key profiles for this user."));
    m_key_profile_combo->setAccessibleName(QCoreApplication::translate("MainWindow.KeyPanel", "Key profile"));
    auto* load_profile_button = new QToolButton(profile_group);
    load_profile_button->setText(QCoreApplication::translate("MainWindow.KeyPanel", "Load"));
    load_profile_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* save_profile_button = new QToolButton(profile_group);
    save_profile_button->setText(QCoreApplication::translate("MainWindow.KeyPanel", "Save"));
    save_profile_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* delete_profile_button = new QToolButton(profile_group);
    delete_profile_button->setText(QCoreApplication::translate("MainWindow.KeyPanel", "Delete"));
    delete_profile_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    profile_layout->addWidget(m_key_profile_combo, 1);
    profile_layout->addWidget(load_profile_button, 0);
    profile_layout->addWidget(save_profile_button, 0);
    profile_layout->addWidget(delete_profile_button, 0);

    auto* cri_group = new QGroupBox(QCoreApplication::translate("MainWindow.KeyPanel", "CRI 64-bit key"), panel);
    auto* cri_form = new QFormLayout(cri_group);
    auto* cri_key_row = make_numeric_key_row(m_cri_key_input, m_cri_key_base_input, cri_group);
    m_cri_key_input->setPlaceholderText(QCoreApplication::translate("MainWindow.KeyPanel", "64-bit key"));
    m_cri_key_input->setToolTip(QCoreApplication::translate("MainWindow.KeyPanel", "Hex mode accepts grouped bytes with spaces. Decimal mode accepts digits only."));
    auto* hca_subkey_row = make_numeric_key_row(m_hca_subkey_input, m_hca_subkey_base_input, cri_group);
    m_hca_subkey_input->setPlaceholderText(QCoreApplication::translate("MainWindow.KeyPanel", "optional 16-bit subkey"));
    cri_form->addRow(QCoreApplication::translate("MainWindow.KeyPanel", "HCA / USM / AWB key"), cri_key_row);
    cri_form->addRow(QCoreApplication::translate("MainWindow.KeyPanel", "HCA subkey"), hca_subkey_row);

    auto* adx_group = new QGroupBox(QCoreApplication::translate("MainWindow.KeyPanel", "ADX / AHX key"), panel);
    auto* adx_form = new QFormLayout(adx_group);
    auto* mode_row = new QWidget(adx_group);
    auto* mode_layout = new QHBoxLayout(mode_row);
    mode_layout->setContentsMargins(0, 0, 0, 0);
    mode_layout->setSpacing(6);
    m_adx_mode_input = new QComboBox(mode_row);
    m_adx_mode_input->setObjectName(QStringLiteral("KeyModeCombo"));
    m_adx_mode_input->setToolTip(QCoreApplication::translate("MainWindow.KeyPanel", "Choose how the ADX/AHX key should be interpreted."));
    m_adx_mode_input->setAccessibleName(QCoreApplication::translate("MainWindow.KeyPanel", "ADX and AHX key mode"));
    m_adx_mode_input->addItem(QCoreApplication::translate("MainWindow.KeyPanel", "None"), static_cast<int>(DecryptionKeys::AdxMode::None));
    m_adx_mode_input->addItem(QCoreApplication::translate("MainWindow.KeyPanel", "Type 8 string"), static_cast<int>(DecryptionKeys::AdxMode::Type8String));
    m_adx_mode_input->addItem(QCoreApplication::translate("MainWindow.KeyPanel", "Type 9 64-bit key"), static_cast<int>(DecryptionKeys::AdxMode::Type9Number));
    m_adx_mode_input->addItem(QCoreApplication::translate("MainWindow.KeyPanel", "Explicit key triplet"), static_cast<int>(DecryptionKeys::AdxMode::AhxTriplet));
    auto* mode_popup_button = new QToolButton(mode_row);
    mode_popup_button->setArrowType(Qt::DownArrow);
    mode_popup_button->setToolTip(QCoreApplication::translate("MainWindow.KeyPanel", "Show key mode choices"));
    mode_popup_button->setAccessibleName(QCoreApplication::translate("MainWindow.KeyPanel", "Show ADX and AHX key mode choices"));
    mode_layout->addWidget(m_adx_mode_input, 1);
    mode_layout->addWidget(mode_popup_button, 0);
    m_adx_string_input = new QLineEdit(adx_group);
    m_adx_string_input->setPlaceholderText(QCoreApplication::translate("MainWindow.KeyPanel", "type-8 key string"));
    auto* adx_number_row = make_numeric_key_row(m_adx_number_input, m_adx_number_base_input, adx_group);
    m_adx_number_input->setPlaceholderText(QCoreApplication::translate("MainWindow.KeyPanel", "64-bit key"));
    m_adx_number_input->setToolTip(QCoreApplication::translate("MainWindow.KeyPanel", "Hex mode accepts grouped bytes with spaces. Decimal mode accepts digits only."));
    auto* adx_subkey_row = make_numeric_key_row(m_adx_subkey_input, m_adx_subkey_base_input, adx_group);
    m_adx_subkey_input->setPlaceholderText(QCoreApplication::translate("MainWindow.KeyPanel", "optional 16-bit subkey"));
    auto* triplet_row = new QWidget(adx_group);
    auto* triplet_layout = new QHBoxLayout(triplet_row);
    triplet_layout->setContentsMargins(0, 0, 0, 0);
    triplet_layout->setSpacing(6);
    m_adx_triplet_start_input = new QLineEdit(triplet_row);
    m_adx_triplet_mult_input = new QLineEdit(triplet_row);
    m_adx_triplet_add_input = new QLineEdit(triplet_row);
    for (auto* input : {m_adx_triplet_start_input, m_adx_triplet_mult_input, m_adx_triplet_add_input}) {
        input->setAlignment(Qt::AlignCenter);
        input->setMaxLength(6);
        input->setMinimumWidth(64);
        input->setPlaceholderText(QStringLiteral("0000"));
        triplet_layout->addWidget(input, 1);
    }
    m_adx_triplet_status = dim_label(QString{}, adx_group);
    adx_form->addRow(QCoreApplication::translate("MainWindow.KeyPanel", "Key mode"), mode_row);
    adx_form->addRow(QCoreApplication::translate("MainWindow.KeyPanel", "String key"), m_adx_string_input);
    adx_form->addRow(QCoreApplication::translate("MainWindow.KeyPanel", "64-bit key"), adx_number_row);
    adx_form->addRow(QCoreApplication::translate("MainWindow.KeyPanel", "Subkey"), adx_subkey_row);
    adx_form->addRow(QCoreApplication::translate("MainWindow.KeyPanel", "Key triplet"), triplet_row);
    adx_form->addRow(QString{}, m_adx_triplet_status);

    auto* hint = new QLabel(
        QCoreApplication::translate("MainWindow.KeyPanel", "Choose hex for copied byte-style keys; spaces are allowed only in hex mode."),
        panel
    );
    hint->setObjectName(QStringLiteral("DimLabel"));
    hint->setWordWrap(true);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    auto* clear_button = new QToolButton(panel);
    clear_button->setText(QCoreApplication::translate("MainWindow.KeyPanel", "Clear"));
    clear_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* apply_button = new QToolButton(panel);
    apply_button->setText(QCoreApplication::translate("MainWindow.KeyPanel", "Apply"));
    apply_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    buttons->addWidget(clear_button, 0);
    buttons->addStretch(1);
    buttons->addWidget(apply_button, 0);

    root->addWidget(profile_group);
    root->addWidget(cri_group);
    root->addWidget(adx_group);
    root->addWidget(hint);
    root->addLayout(buttons);
    root->addStretch(1);

    constexpr auto key_context = "MainWindow.KeyPanel";
    bind_ui_text(m_decryption_keys_window, "windowTitle", "Cryptography Keys", key_context);
    bind_ui_text(profile_group, "title", "Profiles", key_context);
    bind_ui_text(m_key_profile_combo, "toolTip", "Saved cryptography key profiles for this user.", key_context);
    bind_ui_text(m_key_profile_combo, "accessibleName", "Key profile", key_context);
    bind_ui_text(load_profile_button, "text", "Load", key_context);
    bind_ui_text(save_profile_button, "text", "Save", key_context);
    bind_ui_text(delete_profile_button, "text", "Delete", key_context);
    bind_ui_text(cri_group, "title", "CRI 64-bit key", key_context);
    bind_ui_text(m_cri_key_input, "placeholderText", "64-bit key", key_context);
    bind_ui_text(m_cri_key_input, "toolTip", "Hex mode accepts grouped bytes with spaces. Decimal mode accepts digits only.", key_context);
    bind_ui_text(m_hca_subkey_input, "placeholderText", "optional 16-bit subkey", key_context);
    bind_ui_text(cri_form->labelForField(cri_key_row), "text", "HCA / USM / AWB key", key_context);
    bind_ui_text(cri_form->labelForField(hca_subkey_row), "text", "HCA subkey", key_context);
    bind_ui_text(adx_group, "title", "ADX / AHX key", key_context);
    bind_ui_text(m_adx_mode_input, "toolTip", "Choose how the ADX/AHX key should be interpreted.", key_context);
    bind_ui_text(m_adx_mode_input, "accessibleName", "ADX and AHX key mode", key_context);
    bind_ui_text(mode_popup_button, "toolTip", "Show key mode choices", key_context);
    bind_ui_text(mode_popup_button, "accessibleName", "Show ADX and AHX key mode choices", key_context);
    bind_ui_text(m_adx_string_input, "placeholderText", "type-8 key string", key_context);
    bind_ui_text(m_adx_number_input, "placeholderText", "64-bit key", key_context);
    bind_ui_text(m_adx_number_input, "toolTip", "Hex mode accepts grouped bytes with spaces. Decimal mode accepts digits only.", key_context);
    bind_ui_text(m_adx_subkey_input, "placeholderText", "optional 16-bit subkey", key_context);
    bind_ui_text(adx_form->labelForField(mode_row), "text", "Key mode", key_context);
    bind_ui_text(adx_form->labelForField(m_adx_string_input), "text", "String key", key_context);
    bind_ui_text(adx_form->labelForField(adx_number_row), "text", "64-bit key", key_context);
    bind_ui_text(adx_form->labelForField(adx_subkey_row), "text", "Subkey", key_context);
    bind_ui_text(adx_form->labelForField(triplet_row), "text", "Key triplet", key_context);
    bind_ui_text(hint, "text", "Choose hex for copied byte-style keys; spaces are allowed only in hex mode.", key_context);
    bind_ui_text(clear_button, "text", "Clear", key_context);
    bind_ui_text(apply_button, "text", "Apply", key_context);

    m_decryption_keys_window->resize(460, 560);
    if (const auto* target_screen = screen(); target_screen != nullptr) {
        const auto available = target_screen->availableGeometry();
        QRect target(QPoint{}, m_decryption_keys_window->size());
        target.moveCenter(frameGeometry().center());
        target.moveLeft(std::clamp(
            target.left(), available.left(),
            std::max(available.left(), available.right() - target.width() + 1)));
        target.moveTop(std::clamp(
            target.top(), available.top(),
            std::max(available.top(), available.bottom() - target.height() + 1)));
        m_decryption_keys_window->setGeometry(target);
    }
    m_decryption_keys_window->hide();

    const auto update_inputs = [this] { update_decryption_key_derived_fields(); };
    connect(m_adx_mode_input, qOverload<int>(&QComboBox::currentIndexChanged), this, update_inputs);
    connect(mode_popup_button, &QToolButton::clicked, m_adx_mode_input, &QComboBox::showPopup);
    connect(m_adx_string_input, &QLineEdit::textChanged, this, update_inputs);
    connect(m_adx_number_input, &QLineEdit::textChanged, this, update_inputs);
    connect(m_adx_number_base_input, qOverload<int>(&QComboBox::currentIndexChanged), this, update_inputs);
    connect(m_adx_subkey_input, &QLineEdit::textChanged, this, update_inputs);
    connect(m_adx_subkey_base_input, qOverload<int>(&QComboBox::currentIndexChanged), this, update_inputs);
    connect(clear_button, &QToolButton::clicked, this, [this] {
        m_cri_key_input->clear();
        m_hca_subkey_input->clear();
        m_adx_mode_input->setCurrentIndex(0);
        m_adx_string_input->clear();
        m_adx_number_input->clear();
        m_adx_subkey_input->clear();
        m_adx_triplet_start_input->clear();
        m_adx_triplet_mult_input->clear();
        m_adx_triplet_add_input->clear();
        update_decryption_key_derived_fields();
    });
    connect(apply_button, &QToolButton::clicked, this, [this] {
        apply_decryption_key_panel();
    });
    connect(load_profile_button, &QToolButton::clicked, this, [this] {
        load_selected_key_profile();
    });
    connect(save_profile_button, &QToolButton::clicked, this, [this] {
        save_current_key_profile();
    });
    connect(delete_profile_button, &QToolButton::clicked, this, [this] {
        delete_selected_key_profile();
    });
    refresh_key_profile_combo();
}

void MainWindow::retranslate_decryption_keys_window() {
    if (m_decryption_keys_window == nullptr) {
        return;
    }
    constexpr auto context = "MainWindow.KeyPanel";
    for (auto* combo : m_decryption_keys_window->findChildren<QComboBox*>(QStringLiteral("KeyBaseCombo"))) {
        if (combo->count() >= 2) {
            combo->setItemText(0, QCoreApplication::translate(context, "hex"));
            combo->setItemText(1, QCoreApplication::translate(context, "dec"));
        }
        combo->setToolTip(QCoreApplication::translate(context, "Choose how the numeric key text is parsed."));
        combo->setAccessibleName(QCoreApplication::translate(context, "Numeric key base"));
    }
    for (auto* button : m_decryption_keys_window->findChildren<QToolButton*>(QStringLiteral("KeyBaseSegment"))) {
        const bool decimal = button->property("baseValue").toInt() == 10;
        const auto base = QCoreApplication::translate(context, decimal ? "dec" : "hex");
        button->setText(base);
        button->setToolTip(QCoreApplication::translate(context, "Parse this key as %1.").arg(base));
        button->setAccessibleName(QCoreApplication::translate(context, "Use %1 key input").arg(base));
    }
    if (m_adx_mode_input != nullptr && m_adx_mode_input->count() >= 4) {
        m_adx_mode_input->setItemText(0, QCoreApplication::translate(context, "None"));
        m_adx_mode_input->setItemText(1, QCoreApplication::translate(context, "Type 8 string"));
        m_adx_mode_input->setItemText(2, QCoreApplication::translate(context, "Type 9 64-bit key"));
        m_adx_mode_input->setItemText(3, QCoreApplication::translate(context, "Explicit key triplet"));
    }
    update_decryption_key_derived_fields();
}

void MainWindow::sync_decryption_key_panel_from_state() {
    if (m_decryption_keys_window == nullptr) {
        return;
    }

    const QSignalBlocker cri_blocker(m_cri_key_input);
    const QSignalBlocker cri_base_blocker(m_cri_key_base_input);
    const QSignalBlocker hca_blocker(m_hca_subkey_input);
    const QSignalBlocker hca_base_blocker(m_hca_subkey_base_input);
    const QSignalBlocker mode_blocker(m_adx_mode_input);
    const QSignalBlocker string_blocker(m_adx_string_input);
    const QSignalBlocker number_blocker(m_adx_number_input);
    const QSignalBlocker number_base_blocker(m_adx_number_base_input);
    const QSignalBlocker subkey_blocker(m_adx_subkey_input);
    const QSignalBlocker subkey_base_blocker(m_adx_subkey_base_input);
    const QSignalBlocker start_blocker(m_adx_triplet_start_input);
    const QSignalBlocker mult_blocker(m_adx_triplet_mult_input);
    const QSignalBlocker add_blocker(m_adx_triplet_add_input);

    m_cri_key_input->setText(m_decryption_keys.has_cri_key ? key64_display(m_decryption_keys.cri_key) : QString{});
    m_cri_key_base_input->setCurrentIndex(0);
    sync_key_base_selector(m_cri_key_base_input);
    m_hca_subkey_input->setText(m_decryption_keys.hca_subkey == 0 ? QString{} : key16_display(m_decryption_keys.hca_subkey));
    m_hca_subkey_base_input->setCurrentIndex(0);
    sync_key_base_selector(m_hca_subkey_base_input);
    m_adx_mode_input->setCurrentIndex(static_cast<int>(m_decryption_keys.adx_mode));
    m_adx_string_input->setText(utf8_to_qstring(m_decryption_keys.adx_type8_key));
    m_adx_number_input->setText(m_decryption_keys.adx_type9_key == 0 ? QString{} : key64_display(m_decryption_keys.adx_type9_key));
    m_adx_number_base_input->setCurrentIndex(0);
    sync_key_base_selector(m_adx_number_base_input);
    m_adx_subkey_input->setText(m_decryption_keys.adx_subkey == 0 ? QString{} : key16_display(m_decryption_keys.adx_subkey));
    m_adx_subkey_base_input->setCurrentIndex(0);
    sync_key_base_selector(m_adx_subkey_base_input);
    m_adx_triplet_start_input->setText(key16_display(m_decryption_keys.ahx_start));
    m_adx_triplet_mult_input->setText(key16_display(m_decryption_keys.ahx_mult));
    m_adx_triplet_add_input->setText(key16_display(m_decryption_keys.ahx_add));
    update_decryption_key_derived_fields();
}

void MainWindow::update_decryption_key_derived_fields() {
    if (m_decryption_keys_window == nullptr) {
        return;
    }

    const auto mode = static_cast<DecryptionKeys::AdxMode>(m_adx_mode_input->currentData().toInt());
    const auto explicit_triplet = mode == DecryptionKeys::AdxMode::AhxTriplet;
    for (auto* input : {m_adx_triplet_start_input, m_adx_triplet_mult_input, m_adx_triplet_add_input}) {
        input->setReadOnly(!explicit_triplet);
        input->setClearButtonEnabled(explicit_triplet);
    }

    m_adx_string_input->setEnabled(mode == DecryptionKeys::AdxMode::Type8String);
    m_adx_number_input->setEnabled(mode == DecryptionKeys::AdxMode::Type9Number);
    set_key_base_selector_enabled(m_adx_number_base_input, mode == DecryptionKeys::AdxMode::Type9Number);
    m_adx_subkey_input->setEnabled(mode == DecryptionKeys::AdxMode::Type9Number);
    set_key_base_selector_enabled(m_adx_subkey_base_input, mode == DecryptionKeys::AdxMode::Type9Number);

    if (explicit_triplet) {
        m_adx_triplet_status->setText(QCoreApplication::translate("MainWindow.KeyPanel", "explicit key triplet"));
        return;
    }

    QString status;
    cricodecs::adx::AdxKeyState triplet{};
    if (mode == DecryptionKeys::AdxMode::Type8String) {
        if (m_adx_string_input->text().isEmpty()) {
            status = QCoreApplication::translate("MainWindow.KeyPanel", "type-8 string is empty");
        } else {
            triplet = cricodecs::adx::key8_derive(m_adx_string_input->text().toStdString());
            status = QCoreApplication::translate("MainWindow.KeyPanel", "derived from type-8 string");
        }
    } else if (mode == DecryptionKeys::AdxMode::Type9Number) {
        QString error;
        uint64_t key = 0;
        uint16_t subkey = 0;
        if (!parse_u64_key(m_adx_number_input->text(), key_base_value(m_adx_number_base_input), key, error, QCoreApplication::translate("MainWindow.KeyPanel", "ADX type-9 key")) ||
            (!m_adx_subkey_input->text().trimmed().isEmpty() &&
             !parse_u16_key(m_adx_subkey_input->text(), key_base_value(m_adx_subkey_base_input), subkey, error, QCoreApplication::translate("MainWindow.KeyPanel", "ADX subkey")))) {
            status = error;
        } else if (m_adx_number_input->text().trimmed().isEmpty()) {
            status = QCoreApplication::translate("MainWindow.KeyPanel", "type-9 key is empty");
        } else {
            triplet = cricodecs::adx::key9_derive(key, subkey);
            status = QCoreApplication::translate("MainWindow.KeyPanel", "derived from type-9 key");
        }
    } else {
        status = QCoreApplication::translate("MainWindow.KeyPanel", "no ADX/AHX key selected");
    }

    const QSignalBlocker start_blocker(m_adx_triplet_start_input);
    const QSignalBlocker mult_blocker(m_adx_triplet_mult_input);
    const QSignalBlocker add_blocker(m_adx_triplet_add_input);
    m_adx_triplet_start_input->setText(key16_display(triplet.xor_value));
    m_adx_triplet_mult_input->setText(key16_display(triplet.mult));
    m_adx_triplet_add_input->setText(key16_display(triplet.add));
    m_adx_triplet_status->setText(status);
}

bool MainWindow::read_decryption_key_panel(DecryptionKeys& next) {
    if (m_decryption_keys_window == nullptr) {
        return false;
    }

    next = {};
    QString error;

    const auto cri_text = m_cri_key_input->text();
    if (!cri_text.trimmed().isEmpty()) {
        next.has_cri_key = true;
        if (!parse_u64_key(cri_text, key_base_value(m_cri_key_base_input), next.cri_key, error, QCoreApplication::translate("MainWindow.KeyPanel", "CRI key"))) {
            QMessageBox::warning(this, QCoreApplication::translate("MainWindow.KeyPanel", "Invalid key"), error);
            return false;
        }
    }
    if (!m_hca_subkey_input->text().trimmed().isEmpty() &&
        !parse_u16_key(m_hca_subkey_input->text(), key_base_value(m_hca_subkey_base_input), next.hca_subkey, error, QCoreApplication::translate("MainWindow.KeyPanel", "HCA subkey"))) {
        QMessageBox::warning(this, QCoreApplication::translate("MainWindow.KeyPanel", "Invalid key"), error);
        return false;
    }

    next.adx_mode = static_cast<DecryptionKeys::AdxMode>(m_adx_mode_input->currentData().toInt());
    if (next.adx_mode == DecryptionKeys::AdxMode::Type8String) {
        next.adx_type8_key = m_adx_string_input->text().toStdString();
        if (next.adx_type8_key.empty()) {
            QMessageBox::warning(this, QCoreApplication::translate("MainWindow.KeyPanel", "Invalid key"), QCoreApplication::translate("MainWindow.KeyPanel", "ADX type-8 key string is empty."));
            return false;
        }
    } else if (next.adx_mode == DecryptionKeys::AdxMode::Type9Number) {
        if (!parse_u64_key(m_adx_number_input->text(), key_base_value(m_adx_number_base_input), next.adx_type9_key, error, QCoreApplication::translate("MainWindow.KeyPanel", "ADX type-9 key")) ||
            (!m_adx_subkey_input->text().trimmed().isEmpty() &&
             !parse_u16_key(m_adx_subkey_input->text(), key_base_value(m_adx_subkey_base_input), next.adx_subkey, error, QCoreApplication::translate("MainWindow.KeyPanel", "ADX subkey")))) {
            QMessageBox::warning(this, QCoreApplication::translate("MainWindow.KeyPanel", "Invalid key"), error);
            return false;
        }
    } else if (next.adx_mode == DecryptionKeys::AdxMode::AhxTriplet) {
        if (m_adx_triplet_start_input->text().trimmed().isEmpty() ||
            m_adx_triplet_mult_input->text().trimmed().isEmpty() ||
            m_adx_triplet_add_input->text().trimmed().isEmpty() ||
            !parse_u16_key(m_adx_triplet_start_input->text(), 16, next.ahx_start, error, QCoreApplication::translate("MainWindow.KeyPanel", "Key triplet start")) ||
            !parse_u16_key(m_adx_triplet_mult_input->text(), 16, next.ahx_mult, error, QCoreApplication::translate("MainWindow.KeyPanel", "Key triplet mult")) ||
            !parse_u16_key(m_adx_triplet_add_input->text(), 16, next.ahx_add, error, QCoreApplication::translate("MainWindow.KeyPanel", "Key triplet add"))) {
            QMessageBox::warning(
                this,
                QCoreApplication::translate("MainWindow.KeyPanel", "Invalid key"),
                error.isEmpty() ? QCoreApplication::translate("MainWindow.KeyPanel", "Key triplet must be three 16-bit values: start, mult, add.") : error
            );
            return false;
        }
    }

    return true;
}

bool MainWindow::apply_decryption_key_panel() {
    DecryptionKeys next;
    if (!read_decryption_key_panel(next)) {
        return false;
    }

    m_decryption_keys = std::move(next);
    append_log(QCoreApplication::translate("MainWindow.KeyPanel", "Updated session cryptography keys"));
    statusBar()->showMessage(QCoreApplication::translate("MainWindow.KeyPanel", "Updated cryptography keys"), 3000);
    update_document_key_panel(m_file_view == nullptr || !m_file_view->currentIndex().isValid()
        ? nullptr
        : m_file_model->document_at(m_file_proxy->mapToSource(m_file_view->currentIndex()).row()));
    update_key_panel(m_preview_key_panel, m_preview_key_label, m_preview_key_input, m_preview_key_base_input, m_preview_key_apply, m_preview_key_kind);
    if (!reload_current_document_with_keys()) {
        refresh_current_preview();
    }
    return true;
}

void MainWindow::refresh_key_profile_combo() {
    if (m_key_profile_combo == nullptr) {
        return;
    }

    auto settings = key_profile_settings();
    settings.beginGroup(QStringLiteral("keyProfiles"));
    auto names = settings.childGroups();
    settings.endGroup();
    names.sort(Qt::CaseInsensitive);

    const auto current = m_key_profile_combo->currentText();
    const QSignalBlocker blocker(m_key_profile_combo);
    m_key_profile_combo->clear();
    m_key_profile_combo->addItems(names);
    if (!current.isEmpty()) {
        const auto index = m_key_profile_combo->findText(current);
        if (index >= 0) {
            m_key_profile_combo->setCurrentIndex(index);
        }
    }
}

void MainWindow::save_current_key_profile() {
    build_decryption_keys_window();

    DecryptionKeys keys;
    if (!read_decryption_key_panel(keys)) {
        return;
    }

    bool ok = false;
    auto name = QInputDialog::getText(
        this,
        QCoreApplication::translate("MainWindow.KeyPanel", "Save Key Profile"),
        QCoreApplication::translate("MainWindow.KeyPanel", "Profile name"),
        QLineEdit::Normal,
        m_key_profile_combo == nullptr ? QString{} : m_key_profile_combo->currentText(),
        &ok
    ).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        QMessageBox::warning(this, QCoreApplication::translate("MainWindow.KeyPanel", "Invalid profile"), QCoreApplication::translate("MainWindow.KeyPanel", "Profile names cannot contain slashes."));
        return;
    }

    auto settings = key_profile_settings();
    write_key_profile(settings, name, keys);
    settings.sync();
    refresh_key_profile_combo();
    if (m_key_profile_combo != nullptr) {
        const auto index = m_key_profile_combo->findText(name);
        if (index >= 0) {
            m_key_profile_combo->setCurrentIndex(index);
        }
    }
    append_log(QCoreApplication::translate("MainWindow.KeyPanel", "Saved key profile: ") + name);
    statusBar()->showMessage(QCoreApplication::translate("MainWindow.KeyPanel", "Saved key profile: ") + name, 3000);
}

void MainWindow::load_selected_key_profile() {
    build_decryption_keys_window();
    const auto name = m_key_profile_combo == nullptr ? QString{} : m_key_profile_combo->currentText();
    if (name.isEmpty()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.KeyPanel", "No key profile selected"), 3000);
        return;
    }

    auto settings = key_profile_settings();
    DecryptionKeys keys;
    if (!read_key_profile(settings, name, keys)) {
        QMessageBox::warning(this, QCoreApplication::translate("MainWindow.KeyPanel", "Missing profile"), QCoreApplication::translate("MainWindow.KeyPanel", "The selected key profile no longer exists."));
        refresh_key_profile_combo();
        return;
    }

    m_decryption_keys = std::move(keys);
    sync_decryption_key_panel_from_state();
    append_log(QCoreApplication::translate("MainWindow.KeyPanel", "Loaded key profile: ") + name);
    statusBar()->showMessage(QCoreApplication::translate("MainWindow.KeyPanel", "Loaded key profile: ") + name, 3000);
    update_document_key_panel(m_file_view == nullptr || !m_file_view->currentIndex().isValid()
        ? nullptr
        : m_file_model->document_at(m_file_proxy->mapToSource(m_file_view->currentIndex()).row()));
    update_key_panel(m_preview_key_panel, m_preview_key_label, m_preview_key_input, m_preview_key_base_input, m_preview_key_apply, m_preview_key_kind);
    if (!reload_current_document_with_keys()) {
        refresh_current_preview();
    }
}

void MainWindow::delete_selected_key_profile() {
    build_decryption_keys_window();
    const auto name = m_key_profile_combo == nullptr ? QString{} : m_key_profile_combo->currentText();
    if (name.isEmpty()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.KeyPanel", "No key profile selected"), 3000);
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QCoreApplication::translate("MainWindow.KeyPanel", "Delete Key Profile"),
        QCoreApplication::translate("MainWindow.KeyPanel", "Delete key profile \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel
    );
    if (answer != QMessageBox::Yes) {
        return;
    }

    auto settings = key_profile_settings();
    settings.beginGroup(QStringLiteral("keyProfiles"));
    settings.remove(name);
    settings.endGroup();
    settings.sync();
    refresh_key_profile_combo();
    append_log(QCoreApplication::translate("MainWindow.KeyPanel", "Deleted key profile: ") + name);
    statusBar()->showMessage(QCoreApplication::translate("MainWindow.KeyPanel", "Deleted key profile: ") + name, 3000);
}






























void MainWindow::update_document_key_panel(const LoadedDocument* document) {
    if (document == nullptr) {
        m_doc_key_kind = KeyPanelKind::None;
        update_key_panel(m_doc_key_panel, m_doc_key_label, m_doc_key_input, m_doc_key_base_input, m_doc_key_apply, m_doc_key_kind);
        return;
    }

    m_doc_key_kind = key_kind_for_document(*document);
    update_key_panel(m_doc_key_panel, m_doc_key_label, m_doc_key_input, m_doc_key_base_input, m_doc_key_apply, m_doc_key_kind);
}

void MainWindow::update_preview_key_panel(const LoadedDocument* document) {
    if (document == nullptr) {
        m_preview_key_kind = KeyPanelKind::None;
        update_key_panel(m_preview_key_panel, m_preview_key_label, m_preview_key_input, m_preview_key_base_input, m_preview_key_apply, m_preview_key_kind);
        return;
    }

    m_preview_key_kind = key_kind_for_document(*document);
    update_key_panel(m_preview_key_panel, m_preview_key_label, m_preview_key_input, m_preview_key_base_input, m_preview_key_apply, m_preview_key_kind);
}

void MainWindow::update_preview_key_panel(const EntrySummary& entry) {
    m_preview_key_kind = key_kind_for_entry(entry);
    update_key_panel(m_preview_key_panel, m_preview_key_label, m_preview_key_input, m_preview_key_base_input, m_preview_key_apply, m_preview_key_kind);
}

MainWindow::KeyPanelKind MainWindow::key_kind_for_document(const LoadedDocument& document) const {
    const auto format = lower_text(document_format_id(document));
    if (format.contains(QStringLiteral("cvm"))) {
        return KeyPanelKind::None;
    }

    if (format.contains(QStringLiteral("hca"))) {
        if (info_int_value(document.info, "Cipher type") > 1) {
            return KeyPanelKind::Cri64;
        }
        return info_is_yes(document.info, "Encrypted") ? KeyPanelKind::Cri64 : KeyPanelKind::None;
    }

    if (format.contains(QStringLiteral("adx")) || format.contains(QStringLiteral("ahx"))) {
        return info_is_yes(document.info, "Encrypted") ? KeyPanelKind::Adx : KeyPanelKind::None;
    }

    if (format.contains(QStringLiteral("usm"))) {
        return KeyPanelKind::Cri64;
    }

    if (format.contains(QStringLiteral("awb")) || format.contains(QStringLiteral("acb"))) {
        return info_is_yes(document.info, "Encrypted") ||
                       info_contains(document.info, "Key", QStringLiteral("required"))
                   ? KeyPanelKind::Cri64
                   : KeyPanelKind::None;
    }

    if (format.contains(QStringLiteral("aax")) || format.contains(QStringLiteral("aix"))) {
        return info_is_yes(document.info, "Encrypted") ? KeyPanelKind::Adx : KeyPanelKind::None;
    }

    return KeyPanelKind::None;
}

MainWindow::KeyPanelKind MainWindow::key_kind_for_entry(const EntrySummary& entry) const {
    const auto detail = lower_text(entry.detail + " " + entry.type + " " + entry.source_format + " " + entry.nested_source_format);
    if (detail.contains(QStringLiteral("usm"))) {
        return KeyPanelKind::Cri64;
    }

    if (!text_suggests_key_required(detail)) {
        return KeyPanelKind::None;
    }

    if (detail.contains(QStringLiteral("cvm"))) {
        return KeyPanelKind::None;
    }
    if (
        detail.contains(QStringLiteral("hca")) ||
        detail.contains(QStringLiteral("usm")) ||
        detail.contains(QStringLiteral("awb")) ||
        detail.contains(QStringLiteral("acb")) ||
        detail.contains(QStringLiteral("sfa")) ||
        detail.contains(QStringLiteral("sfv"))
    ) {
        return KeyPanelKind::Cri64;
    }
    if (
        detail.contains(QStringLiteral("adx")) ||
        detail.contains(QStringLiteral("ahx")) ||
        detail.contains(QStringLiteral("aax")) ||
        detail.contains(QStringLiteral("aix"))
    ) {
        return KeyPanelKind::Adx;
    }

    return KeyPanelKind::None;
}

void MainWindow::update_key_panel(
    QWidget* panel,
    QLabel* label,
    QLineEdit* input,
    QComboBox* base,
    QToolButton* apply,
    KeyPanelKind kind
) {
    if (panel == nullptr || label == nullptr || input == nullptr || base == nullptr || apply == nullptr) {
        return;
    }

    panel->setVisible(kind != KeyPanelKind::None);
    if (kind == KeyPanelKind::None) {
        input->clear();
        set_key_base_selector_visible(base, false);
        return;
    }

    QSignalBlocker blocker(input);
    QSignalBlocker base_blocker(base);
    if (kind == KeyPanelKind::Cri64) {
        label->setText(QCoreApplication::translate("MainWindow.KeyPanel", "CRI key"));
        input->setPlaceholderText(QCoreApplication::translate("MainWindow.KeyPanel", "64-bit key"));
        input->setToolTip(QCoreApplication::translate("MainWindow.KeyPanel", "Hex mode accepts spaces and underscores. Decimal mode accepts digits and underscores only."));
        input->setText(m_decryption_keys.has_cri_key ? key64_display(m_decryption_keys.cri_key) : QString{});
        base->setCurrentIndex(0);
        sync_key_base_selector(base);
        set_key_base_selector_visible(base, true);
    } else if (kind == KeyPanelKind::Adx) {
        label->setText(QCoreApplication::translate("MainWindow.KeyPanel", "ADX/AHX key"));
        input->setPlaceholderText(QCoreApplication::translate("MainWindow.KeyPanel", "type8:string, key, or start,mult,add"));
        input->setToolTip(QCoreApplication::translate("MainWindow.KeyPanel", "Numeric type-9 keys use the selected base. Key triplets are parsed as hex."));
        input->setText(adx_key_display(m_decryption_keys));
        base->setCurrentIndex(0);
        sync_key_base_selector(base);
        set_key_base_selector_visible(base, true);
    }
}

void MainWindow::apply_key_panel_value(KeyPanelKind kind, const QString& value, int numeric_base) {
    if (kind == KeyPanelKind::None) {
        return;
    }

    QString error;
    if (kind == KeyPanelKind::Cri64) {
        const auto text = value;
        if (text.trimmed().isEmpty()) {
            m_decryption_keys.has_cri_key = false;
            m_decryption_keys.cri_key = 0;
        } else if (!parse_u64_key(text, numeric_base, m_decryption_keys.cri_key, error, QCoreApplication::translate("MainWindow.KeyPanel", "CRI key"))) {
            QMessageBox::warning(this, QCoreApplication::translate("MainWindow.KeyPanel", "Invalid key"), error);
            return;
        } else {
            m_decryption_keys.has_cri_key = true;
        }
        update_document_key_panel(m_file_view == nullptr || !m_file_view->currentIndex().isValid()
            ? nullptr
            : m_file_model->document_at(m_file_proxy->mapToSource(m_file_view->currentIndex()).row()));
        update_key_panel(m_preview_key_panel, m_preview_key_label, m_preview_key_input, m_preview_key_base_input, m_preview_key_apply, m_preview_key_kind);
        refresh_current_preview();
        return;
    }

    const auto text = value.trimmed();
    if (text.isEmpty()) {
        m_decryption_keys.adx_mode = DecryptionKeys::AdxMode::None;
        m_decryption_keys.adx_type8_key.clear();
        m_decryption_keys.adx_type9_key = 0;
        m_decryption_keys.adx_subkey = 0;
        m_decryption_keys.ahx_start = 0;
        m_decryption_keys.ahx_mult = 0;
        m_decryption_keys.ahx_add = 0;
    } else if (text.contains(QLatin1Char(','))) {
        const auto parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() != 3 ||
            !parse_u16_key(parts[0], 16, m_decryption_keys.ahx_start, error, QCoreApplication::translate("MainWindow.KeyPanel", "AHX start")) ||
            !parse_u16_key(parts[1], 16, m_decryption_keys.ahx_mult, error, QCoreApplication::translate("MainWindow.KeyPanel", "AHX mult")) ||
            !parse_u16_key(parts[2], 16, m_decryption_keys.ahx_add, error, QCoreApplication::translate("MainWindow.KeyPanel", "AHX add"))) {
            QMessageBox::warning(this, QCoreApplication::translate("MainWindow.KeyPanel", "Invalid key"), error.isEmpty()
                ? QCoreApplication::translate("MainWindow.KeyPanel", "AHX triplet must be three 16-bit values: start,mult,add.")
                : error);
            return;
        }
        m_decryption_keys.adx_mode = DecryptionKeys::AdxMode::AhxTriplet;
    } else if (text.startsWith(QStringLiteral("type8:"), Qt::CaseInsensitive)) {
        m_decryption_keys.adx_mode = DecryptionKeys::AdxMode::Type8String;
        m_decryption_keys.adx_type8_key = text.mid(6).toStdString();
    } else if (is_adx_key_numeric_candidate(text)) {
        if (!parse_u64_key(text, numeric_base, m_decryption_keys.adx_type9_key, error, QCoreApplication::translate("MainWindow.KeyPanel", "ADX type-9 key"))) {
            QMessageBox::warning(this, QCoreApplication::translate("MainWindow.KeyPanel", "Invalid key"), error);
            return;
        }
        m_decryption_keys.adx_mode = DecryptionKeys::AdxMode::Type9Number;
    } else {
        m_decryption_keys.adx_mode = DecryptionKeys::AdxMode::Type8String;
        m_decryption_keys.adx_type8_key = text.toStdString();
    }

    update_document_key_panel(m_file_view == nullptr || !m_file_view->currentIndex().isValid()
        ? nullptr
        : m_file_model->document_at(m_file_proxy->mapToSource(m_file_view->currentIndex()).row()));
    update_key_panel(m_preview_key_panel, m_preview_key_label, m_preview_key_input, m_preview_key_base_input, m_preview_key_apply, m_preview_key_kind);
    refresh_current_preview();
}



} // namespace cristudio
