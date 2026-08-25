#pragma once

#include "editor/editor_helpers.hpp"
#include "editor_workspace.hpp"

#include "acx_container.hpp"
#include "afs_container.hpp"
#include "awb_container.hpp"
#include "cpk_container.hpp"
#include "cvm_container.hpp"
#include "utf_table.hpp"

#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cristudio {

struct ScratchEditorSession {
    std::optional<LoadedDocument> document;
    std::vector<uint8_t> bytes;
    std::optional<cricodecs::utf::UtfTable> utf;
    std::optional<cricodecs::afs::AfsContainer> afs;
    std::optional<cricodecs::awb::AwbContainer> awb;
    std::optional<cricodecs::acx::AcxContainer> acx;
    std::optional<cricodecs::cpk::Cpk> cpk;
    std::optional<cricodecs::cvm::CvmContainer> cvm;
    ArchiveKind archive_kind = ArchiveKind::None;
    TransformKind transform_kind = TransformKind::None;
    std::optional<std::string> title;
    std::vector<QString> log_messages;
};

[[nodiscard]] ScratchEditorSession create_scratch_editor_session(const EditorOpenRequest& request);

} // namespace cristudio
