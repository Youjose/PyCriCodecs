#pragma once

#include "document/document_types.hpp"

#include <QColor>
#include <QString>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cristudio {

struct HexPatternRange {
    uint64_t offset = 0;
    uint64_t size = 0;
    QString label;
    QColor color;
};

enum class HexPatternValueKind : uint8_t {
    None,
    U8,
    U16BE,
    U16LE,
    U32BE,
    U32LE,
    HexBytes
};

struct HexPatternRepeatField {
    uint64_t offset = 0;
    uint64_t size = 0;
    QString name;
    QColor color;
    HexPatternValueKind value_kind = HexPatternValueKind::None;
};

struct HexPatternRepeat {
    uint64_t offset = 0;
    uint64_t stride = 0;
    uint64_t size = 0;
    uint64_t count = 0;
    QString label;
    QColor color;
    std::vector<HexPatternRepeatField> fields;
};

struct HexPatternSet {
    std::vector<HexPatternRange> ranges;
    std::vector<HexPatternRepeat> repeats;
};

[[nodiscard]] HexPatternSet infer_hex_patterns(
    std::string_view format,
    uint64_t total_size,
    std::span<const uint8_t> prefix
);

[[nodiscard]] HexPatternSet infer_document_hex_patterns(
    const LoadedDocument& document,
    std::span<const uint8_t> prefix
);

[[nodiscard]] HexPatternSet infer_entry_hex_patterns(
    const EntrySummary& entry,
    uint64_t total_size,
    std::span<const uint8_t> prefix
);

[[nodiscard]] std::string entry_hex_format(const EntrySummary& entry);

} // namespace cristudio
