#include "modules/adx/adx_edit_ui.hpp"

#include "editor/editor_widgets.hpp"
#include "modules/adx/adx_common.hpp"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cstdint>
#include <limits>
#include <utility>

namespace cristudio::modules::adx {
std::vector<TransformDetailRow> detail_rows(const cricodecs::adx::Adx& adx) {
    std::vector<TransformDetailRow> rows;
    const auto& header = adx.header();
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Signature"), QStringLiteral("0x%1").arg(header.signature, 4, 16, QLatin1Char('0')).toUpper()});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Data offset"), QString::number(header.data_offset)});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Encoding mode"), QString::number(header.encoding_mode)});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Block size"), QString::number(header.block_size)});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Bit depth"), QString::number(header.bit_depth)});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Channels"), QString::number(header.channels)});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Sample rate"), QString::number(header.sample_rate)});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Sample count"), QString::number(header.sample_count)});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Highpass frequency"), QString::number(header.highpass_freq)});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Version"), QString::number(header.version)});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Flags"), QStringLiteral("0x%1").arg(header.flags, 2, 16, QLatin1Char('0')).toUpper()});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Encrypted"), adx.is_encrypted() ? QCoreApplication::translate("Adx.AdxEditUi", "yes") : QCoreApplication::translate("Adx.AdxEditUi", "no")});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "AHX routed"), adx.is_ahx() ? QCoreApplication::translate("Adx.AdxEditUi", "yes") : QCoreApplication::translate("Adx.AdxEditUi", "no")});
    rows.push_back({QCoreApplication::translate("Adx.AdxEditUi", "Loop count"), QString::number(static_cast<qsizetype>(adx.loops().size()))});
    for (const auto& loop : adx.loops()) {
        rows.push_back({
            QCoreApplication::translate("Adx.AdxEditUi", "Loop %1").arg(loop.index),
            QCoreApplication::translate("Adx.AdxEditUi", "type %1, samples %2-%3, bytes %4-%5")
                .arg(loop.type)
                .arg(loop.start_sample)
                .arg(loop.end_sample)
                .arg(loop.start_byte)
                .arg(loop.end_byte)
        });
    }
    return rows;
}

std::expected<std::optional<cricodecs::adx::AdxEncodeConfig>, QString> choose_rebuild_config(
    QWidget* parent,
    const cricodecs::adx::Adx& adx,
    const DecryptionKeys& keys
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Adx.AdxEditUi", "ADX/AHX rebuild options"));
    dialog.setMinimumWidth(500);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(14);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    const auto& header = adx.header();
    const bool is_ahx = adx.is_ahx();
    auto* mode_combo = new QComboBox(&dialog);
    if (is_ahx) {
        mode_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "AHX 0x10"), 0x10);
        mode_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "AHX 0x11 (encoder delay)"), 0x11);
    } else {
        mode_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "Mode 2 - fixed coefficient"), 2);
        mode_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "Mode 3 - linear prediction"), 3);
        mode_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "Mode 4 - exponential scale"), 4);
    }
    if (const auto index = mode_combo->findData(header.encoding_mode); index >= 0) {
        mode_combo->setCurrentIndex(index);
    }
    QComboBox* version_combo = nullptr;
    QSpinBox* highpass_spin = nullptr;
    QCheckBox* trim_check = nullptr;
    if (!is_ahx) {
        version_combo = new QComboBox(&dialog);
        version_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "Version 3"), 3);
        version_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "Version 4"), 4);
        version_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "Version 5"), 5);
        if (const auto index = version_combo->findData(header.version); index >= 0) {
            version_combo->setCurrentIndex(index);
        }
        highpass_spin = new QSpinBox(&dialog);
        highpass_spin->setRange(0, 24000);
        highpass_spin->setValue(header.highpass_freq);
        highpass_spin->setSuffix(QCoreApplication::translate("Adx.AdxEditUi", " Hz"));
        trim_check = new QCheckBox(QCoreApplication::translate("Adx.AdxEditUi", "Trim samples after first loop end"), &dialog);
    }
    auto* encrypt_combo = new QComboBox(&dialog);
    encrypt_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "Plain"), 0);
    const bool can_write_type8 = keys.adx_mode == DecryptionKeys::AdxMode::Type8String
        || (is_ahx && keys.adx_mode == DecryptionKeys::AdxMode::AhxTriplet);
    const bool can_write_type9 = keys.adx_mode == DecryptionKeys::AdxMode::Type9Number
        || (is_ahx && keys.adx_mode == DecryptionKeys::AdxMode::AhxTriplet);
    if (can_write_type8) {
        encrypt_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "Type 8 - use local key"), 8);
    }
    if (can_write_type9) {
        encrypt_combo->addItem(QCoreApplication::translate("Adx.AdxEditUi", "Type 9 - use local key"), 9);
    }
    if (const auto index = encrypt_combo->findData(header.flags == 8 || header.flags == 9 ? header.flags : 0); index >= 0) {
        encrypt_combo->setCurrentIndex(index);
    }
    form->addRow(is_ahx ? QCoreApplication::translate("Adx.AdxEditUi", "AHX profile") : QCoreApplication::translate("Adx.AdxEditUi", "Encoding mode"), mode_combo);
    if (!is_ahx) {
        form->addRow(QCoreApplication::translate("Adx.AdxEditUi", "Version"), version_combo);
        form->addRow(QCoreApplication::translate("Adx.AdxEditUi", "Highpass"), highpass_spin);
    }
    form->addRow(QCoreApplication::translate("Adx.AdxEditUi", "Encryption"), encrypt_combo);
    if (!is_ahx) {
        form->addRow(QCoreApplication::translate("Adx.AdxEditUi", "Loop policy"), trim_check);
    }
    layout->addLayout(form);

    auto* note = dim_label(is_ahx
        ? QCoreApplication::translate("Adx.AdxEditUi", "AHX rebuild uses MPEG Layer II profiles. Encrypted output is offered only when this tab has a compatible local key.")
        : QCoreApplication::translate("Adx.AdxEditUi", "ADX rebuild decodes with the session key, then re-encodes. Encrypted output is offered only for a compatible local key type."), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = dialog_buttons(
        dialog, QCoreApplication::translate("Adx.AdxEditUi", "Rebuild Session"));
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<cricodecs::adx::AdxEncodeConfig>{};
    }
    if (!has_compatible_key(adx, keys)) {
        return std::unexpected(adx.is_ahx()
            ? QCoreApplication::translate("Adx.AdxEditUi", "This encrypted AHX input needs a compatible local key before it can be decoded and rebuilt.")
            : QCoreApplication::translate("Adx.AdxEditUi", "This encrypted ADX input needs a compatible local key before it can be decoded and rebuilt."));
    }

    cricodecs::adx::AdxEncodeConfig config;
    config.encoding_mode = static_cast<uint8_t>(mode_combo->currentData().toUInt());
    config.version = is_ahx ? header.version : static_cast<uint8_t>(version_combo->currentData().toUInt());
    config.highpass_freq = is_ahx ? header.highpass_freq : static_cast<uint16_t>(highpass_spin->value());
    config.encryption_type = static_cast<uint8_t>(encrypt_combo->currentData().toUInt());
    config.delete_samples_after_loop_end = !is_ahx && trim_check->isChecked();
    return std::optional<cricodecs::adx::AdxEncodeConfig>(config);
}

} // namespace cristudio::modules::adx
