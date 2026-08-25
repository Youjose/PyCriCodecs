#pragma once

#include "editor/editor_helpers.hpp"

#include "awb_container.hpp"
#include "cvm_container.hpp"

#include <QString>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cricodecs::afs {
class AfsContainer;
}

namespace cricodecs::acx {
class AcxContainer;
}

namespace cricodecs::cpk {
class Cpk;
}

class QWidget;

namespace cristudio {

struct ArchiveSessionView {
    ArchiveKind kind = ArchiveKind::None;
    const cricodecs::afs::AfsContainer* afs = nullptr;
    const cricodecs::awb::AwbContainer* awb = nullptr;
    const cricodecs::acx::AcxContainer* acx = nullptr;
    const cricodecs::cpk::Cpk* cpk = nullptr;
    const cricodecs::cvm::CvmContainer* cvm = nullptr;
};

struct MutableArchiveSessionView {
    ArchiveKind kind = ArchiveKind::None;
    cricodecs::afs::AfsContainer* afs = nullptr;
    cricodecs::awb::AwbContainer* awb = nullptr;
    cricodecs::acx::AcxContainer* acx = nullptr;
    cricodecs::cpk::Cpk* cpk = nullptr;
    cricodecs::cvm::CvmContainer* cvm = nullptr;
    bool* cpk_obfuscate_utf = nullptr;
};

struct ArchiveItemEditResult {
    bool handled = false;
    bool changed = false;
    QString warning_title;
    QString error;
    QString change_message;
    int selected_row = -1;
};

[[nodiscard]] int validated_archive_index(const ArchiveSessionView& view, int row);
[[nodiscard]] ArchiveItemEditResult edit_archive_table_item(
    const MutableArchiveSessionView& view,
    int row,
    int column,
    const QString& text
);
[[nodiscard]] EditorBuildResult build_archive_session_bytes(
    const MutableArchiveSessionView& view
);
[[nodiscard]] ArchiveItemEditResult edit_archive_entry_properties(
    QWidget* parent,
    const MutableArchiveSessionView& view,
    int row
);
[[nodiscard]] ArchiveItemEditResult edit_archive_options(
    QWidget* parent,
    const MutableArchiveSessionView& view
);
[[nodiscard]] ArchiveItemEditResult add_archive_file(QWidget* parent, const MutableArchiveSessionView& view);
[[nodiscard]] ArchiveItemEditResult replace_archive_file(QWidget* parent, const MutableArchiveSessionView& view, int index);
[[nodiscard]] ArchiveItemEditResult remove_archive_file(QWidget* parent, const MutableArchiveSessionView& view, int index);
[[nodiscard]] ArchiveItemEditResult move_archive_entry(const MutableArchiveSessionView& view, int index, int delta);
[[nodiscard]] QString archive_entry_default_name(const ArchiveSessionView& view, uint32_t index);
[[nodiscard]] std::expected<std::span<const uint8_t>, QString> archive_entry_bytes(
    const ArchiveSessionView& view,
    uint32_t index,
    std::vector<uint8_t>& scratch
);
[[nodiscard]] QString archive_entry_preview_text(
    const ArchiveSessionView& view,
    uint32_t index,
    std::span<const uint8_t> bytes
);

} // namespace cristudio
