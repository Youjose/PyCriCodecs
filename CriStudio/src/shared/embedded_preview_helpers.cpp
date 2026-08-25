#include "shared/embedded_preview_helpers.hpp"

#include <QCoreApplication>

#include <algorithm>

namespace cristudio {
bool is_supported_image_payload(std::span<const uint8_t> bytes) {
    return (bytes.size() >= 8 &&
               bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
               bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) ||
           (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) ||
           (bytes.size() >= 6 &&
               bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == '8') ||
           (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') ||
           (bytes.size() >= 12 &&
               bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
               bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P');
}

QString hex_preview(std::span<const uint8_t> bytes, size_t max_bytes) {
    if (bytes.empty()) {
        return QCoreApplication::translate("Editor.EditorHelpers", "(no bytes)");
    }

    const auto shown = std::min(bytes.size(), max_bytes);
    QString out;
    out.reserve(static_cast<qsizetype>(shown * 5 + 128));
    for (size_t offset = 0; offset < shown; offset += 16) {
        const auto row_end = std::min(offset + 16, shown);
        out += QStringLiteral("%1  |  ").arg(static_cast<qulonglong>(offset), 8, 16, QLatin1Char('0')).toUpper();
        for (size_t i = offset; i < offset + 16; ++i) {
            out += i < row_end
                ? QStringLiteral("%1 ").arg(bytes[i], 2, 16, QLatin1Char('0')).toUpper()
                : QStringLiteral("   ");
        }
        out += QStringLiteral(" | ");
        for (size_t i = offset; i < row_end; ++i) {
            const auto ch = bytes[i];
            out += ch >= 0x20 && ch <= 0x7E ? QLatin1Char(static_cast<char>(ch)) : QLatin1Char('.');
        }
        out += QLatin1Char('\n');
    }
    if (bytes.size() > shown) {
        out += QCoreApplication::translate("Editor.EditorHelpers", "... truncated, %1 total bytes ...\n")
            .arg(static_cast<qulonglong>(bytes.size()));
    }
    return out;
}

} // namespace cristudio
