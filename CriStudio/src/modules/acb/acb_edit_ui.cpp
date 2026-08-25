#include "modules/acb/acb_edit_ui.hpp"

#include "modules/acb/acb_edit.hpp"
#include "path_text.hpp"

#include <QCoreApplication>
#include <QFileDialog>

#include <string>
#include <utility>

namespace cristudio::modules::acb {
std::expected<AssociatedAwbOpenPayload, QString> prepare_associated_awb_open(
    QWidget*,
    const cricodecs::acb::AcbContainer& acb,
    QString title,
    DecryptionKeys keys,
    const std::filesystem::path& source_archive_path
) {
    auto awb = associated_awb_bytes(acb);
    if (!awb) {
        return std::unexpected(awb.error());
    }

    auto validate = validate_associated_awb(awb->bytes);
    if (!validate) {
        return std::unexpected(validate.error());
    }

    return AssociatedAwbOpenPayload{
        .display_name = qstring_to_utf8(associated_awb_default_name(acb, std::move(title))),
        .keys = std::move(keys),
        .source_path = awb->source_path.value_or(std::filesystem::path{}),
        .source_archive_path = source_archive_path,
        .bytes = std::move(awb->bytes)
    };
}

std::expected<std::optional<AssociatedAwbExportPayload>, QString> choose_associated_awb_export(
    QWidget* parent,
    const cricodecs::acb::AcbContainer& acb,
    QString title
) {
    auto awb = associated_awb_bytes(acb);
    if (!awb) {
        return std::unexpected(awb.error());
    }

    const auto path_text = QFileDialog::getSaveFileName(
        parent,
        QCoreApplication::translate("Acb.AcbEditUi", "Export associated AWB"),
        associated_awb_default_name(acb, std::move(title)),
        QCoreApplication::translate("Acb.AcbEditUi", "AWB/AFS2 banks (*.awb);;All files (*)")
    );
    if (path_text.isEmpty()) {
        return std::optional<AssociatedAwbExportPayload>{};
    }

    return std::optional<AssociatedAwbExportPayload>(AssociatedAwbExportPayload{
        .output_path = path_from_qstring(path_text),
        .bytes = std::move(awb->bytes)
    });
}

} // namespace cristudio::modules::acb
