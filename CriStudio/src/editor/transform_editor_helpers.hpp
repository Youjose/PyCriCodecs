#pragma once

#include "document/document_types.hpp"
#include "editor/editor_helpers.hpp"
#include "modules/transform_detail.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

class QWidget;

namespace cricodecs::adx { class Adx; }
namespace cricodecs::aax { class AaxContainer; }
namespace cricodecs::acb { class AcbContainer; }
namespace cricodecs::aix { class Aix; }
namespace cricodecs::csb { class CsbContainer; }
namespace cricodecs::hca { class Hca; }
namespace cricodecs::sfd { class SfdContainer; }
namespace cricodecs::usm { class UsmReader; }

namespace cristudio {

struct TransformPayloadSelection {
    int payload_kind = 0;
    int index = 0;
    int layer = 0;
};

struct TransformSessionView {
    cricodecs::adx::Adx* adx = nullptr;
    cricodecs::hca::Hca* hca = nullptr;
    cricodecs::aax::AaxContainer* aax = nullptr;
    cricodecs::aix::Aix* aix = nullptr;
    cricodecs::usm::UsmReader* usm = nullptr;
    cricodecs::sfd::SfdContainer* sfd = nullptr;
    cricodecs::csb::CsbContainer* csb = nullptr;
    cricodecs::acb::AcbContainer* acb = nullptr;
    const DecryptionKeys* keys = nullptr;
};

[[nodiscard]] QString transform_payload_preview_text(
    const TransformSessionView& view,
    TransformPayloadSelection selection,
    std::span<const uint8_t> fallback_bytes
);

[[nodiscard]] std::expected<std::vector<uint8_t>, QString> transform_payload_preview_bytes(
    const TransformSessionView& view,
    TransformPayloadSelection selection
);

[[nodiscard]] std::vector<modules::TransformDetailRow> transform_detail_rows(
    TransformKind kind,
    const TransformSessionView& view
);

void append_transform_info_rows(
    std::vector<InfoRow>& rows,
    TransformKind kind,
    const TransformSessionView& view,
    size_t max_rows = 16
);

[[nodiscard]] EditorBuildResult build_transform_session_bytes(
    TransformKind kind,
    const TransformSessionView& view
);

[[nodiscard]] EditorBuildResult edit_transform_options(
    QWidget* parent,
    TransformKind kind,
    const TransformSessionView& view
);

} // namespace cristudio
