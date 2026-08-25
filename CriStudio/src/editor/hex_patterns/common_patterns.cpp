#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <optional>

namespace cristudio::hexpatterns {

[[nodiscard]] QColor tone(int index) {
    static const std::array<QColor, 8> colors = {
        QColor(0, 188, 255),
        QColor(255, 190, 48),
        QColor(80, 230, 130),
        QColor(185, 120, 255),
        QColor(255, 90, 135),
        QColor(35, 225, 205),
        QColor(255, 135, 40),
        QColor(170, 190, 210),
    };
    auto color = colors[static_cast<size_t>(index) % colors.size()];
    color.setAlpha(122);
    return color;
}

void add(std::vector<HexPatternRange>& out, uint64_t offset, uint64_t size, QString label, QColor color, uint64_t total_size) {
    if (size == 0 || offset >= total_size) {
        return;
    }
    const auto clipped = std::min<uint64_t>(size, total_size - offset);
    out.push_back(HexPatternRange{
        .offset = offset,
        .size = clipped,
        .label = std::move(label),
        .color = color
    });
}

void add_repeat(
    std::vector<HexPatternRepeat>& out,
    uint64_t offset,
    uint64_t stride,
    uint64_t size,
    uint64_t count,
    QString label,
    QColor color,
    std::vector<HexPatternRepeatField> fields,
    uint64_t total_size
) {
    if (stride == 0 || size == 0 || count == 0 || offset >= total_size) {
        return;
    }
    const auto available = (total_size - offset) / stride;
    const auto clipped_count = std::min<uint64_t>(count, available == 0 ? 1 : available);
    if (clipped_count == 0) {
        return;
    }
    out.push_back(HexPatternRepeat{
        .offset = offset,
        .stride = stride,
        .size = std::min<uint64_t>(size, stride),
        .count = clipped_count,
        .label = std::move(label),
        .color = color,
        .fields = std::move(fields)
    });
}

[[nodiscard]] bool has(std::span<const uint8_t> bytes, size_t offset, std::string_view text) {
    return offset + text.size() <= bytes.size() &&
        std::equal(text.begin(), text.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] std::optional<uint64_t> decimal_prefix(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    } else if (!std::isdigit(static_cast<unsigned char>(text.front()))) {
        return std::nullopt;
    }
    uint64_t value = 0;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(first, last, value, base);
    if (ec != std::errc{} || ptr == first) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<uint64_t> byte_size_prefix(std::string_view text) {
    return decimal_prefix(text);
}

[[nodiscard]] std::optional<uint64_t> info_value(const LoadedDocument& document, std::string_view name) {
    for (const auto& row : document.info) {
        if (info_row_id(row) == name) {
            return decimal_prefix(row.value);
        }
    }
    return std::nullopt;
}

[[nodiscard]] QString hex_value(uint64_t value, int width) {
    return QStringLiteral("0x%1")
        .arg(static_cast<qulonglong>(value), width, 16, QLatin1Char('0'))
        .toUpper();
}

[[nodiscard]] QString ascii_value(std::span<const uint8_t> bytes, size_t offset, size_t size) {
    if (offset + size > bytes.size()) {
        return {};
    }
    return QString::fromLatin1(reinterpret_cast<const char*>(bytes.data() + offset), static_cast<qsizetype>(size));
}

void add_field(
    std::vector<HexPatternRange>& out,
    uint64_t offset,
    uint64_t size,
    QString name,
    QString value,
    int color_index,
    uint64_t total_size
) {
    add(out, offset, size, QStringLiteral("%1 = %2").arg(name, value), tone(color_index), total_size);
}

} // namespace cristudio::hexpatterns
