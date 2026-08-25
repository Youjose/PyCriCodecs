#include "editor/hex_preview_widget.hpp"

#include "io_reader.hpp"
#include "path_text.hpp"

#include <QCoreApplication>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <limits>
#include <utility>

namespace cristudio {
namespace {

QChar text_char(uint8_t value) {
    if (value >= 0x20 && value <= 0x7E) {
        return QLatin1Char(static_cast<char>(value));
    }
    if (value >= 0x80 && value <= 0x9F) {
        static constexpr char32_t cp1252[] = {
            0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
            0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
        };
        const auto mapped = cp1252[value - 0x80];
        return mapped == 0 ? QLatin1Char('.') : QChar(static_cast<char16_t>(mapped));
    }
    if (value >= 0xA0) {
        return QChar(value);
    }
    return QLatin1Char('.');
}

QColor readable_on(QColor color, const QPalette& palette) {
    color = color.toRgb();
    const auto luminance =
        0.2126 * color.redF() +
        0.7152 * color.greenF() +
        0.0722 * color.blueF();
    return luminance < 0.45 ? palette.color(QPalette::HighlightedText) : QColor(18, 28, 32);
}

bool is_dark_color(QColor color) {
    color = color.toRgb();
    const auto luminance =
        0.2126 * color.redF() +
        0.7152 * color.greenF() +
        0.0722 * color.blueF();
    return luminance < 0.35;
}

bool is_lazy_usm_format(std::string_view format) {
    const auto lower = lower_ascii(format);
    return lower.find("usm") != std::string::npos || lower.find("sofdec") != std::string::npos;
}

bool is_lazy_sbt_format(std::string_view format) {
    const auto lower = lower_ascii(format);
    return lower.find("sbt") != std::string::npos && lower.find("subtitle") != std::string::npos;
}

bool is_lazy_riff_format(std::string_view format) {
    const auto lower = lower_ascii(format);
    return lower.find("wav") != std::string::npos || lower.find("riff") != std::string::npos;
}

bool is_lazy_aix_format(std::string_view format) {
    return lower_ascii(format).find("aix") != std::string::npos;
}

bool is_lazy_cvm_format(std::string_view format) {
    return lower_ascii(format).find("cvm") != std::string::npos;
}

uint64_t pattern_end(const HexPatternRange& pattern) {
    return pattern.size > std::numeric_limits<uint64_t>::max() - pattern.offset
        ? std::numeric_limits<uint64_t>::max()
        : pattern.offset + pattern.size;
}

bool has_magic(std::span<const uint8_t> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() &&
        std::equal(magic.begin(), magic.end(), bytes.begin());
}

bool is_usm_chunk_magic(std::span<const uint8_t> bytes) {
    return has_magic(bytes, "CRID") || has_magic(bytes, "@SFV") || has_magic(bytes, "@SFA") ||
        has_magic(bytes, "@SBT") || has_magic(bytes, "@ALP") || has_magic(bytes, "@AHX") ||
        has_magic(bytes, "@ELM") || has_magic(bytes, "@ATP") || has_magic(bytes, "@PST") ||
        has_magic(bytes, "@CUE") || has_magic(bytes, "@STA") || has_magic(bytes, "@USR") ||
        has_magic(bytes, "SFSH");
}

QString magic_text(std::span<const uint8_t> bytes) {
    return QString::fromLatin1(reinterpret_cast<const char*>(bytes.data()), 4);
}

QColor lazy_usm_color(uint64_t seed) {
    static const std::array<QColor, 5> colors = {
        QColor(255, 190, 48, 112),
        QColor(80, 230, 130, 112),
        QColor(185, 120, 255, 112),
        QColor(255, 90, 135, 112),
        QColor(35, 225, 205, 112),
    };
    return colors[static_cast<size_t>(seed % colors.size())];
}

QString hex_byte(uint8_t value) {
    return QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

QString hex_word(uint32_t value) {
    return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
}

QString offset_text(uint64_t value, int base) {
    return base == 10
        ? QString::number(static_cast<qulonglong>(value))
        : QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 0, 16).toUpper();
}

std::optional<uint64_t> parse_offset_text(QString text, int base) {
    text = text.trimmed();
    text.remove(QLatin1Char('_'));
    text.remove(QLatin1Char(' '));
    if (text.isEmpty()) {
        return std::nullopt;
    }
    if (base == 16 && text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        text = text.mid(2);
    }
    bool ok = false;
    const auto value = text.toULongLong(&ok, base);
    return ok ? std::optional<uint64_t>(value) : std::nullopt;
}

void sync_offset_base_buttons(QWidget* selector, int active_base) {
    if (selector == nullptr) {
        return;
    }
    const auto buttons = selector->findChildren<QToolButton*>(QStringLiteral("OffsetBaseSegment"));
    for (auto* button : buttons) {
        const QSignalBlocker blocker(button);
        button->setChecked(button->property("baseValue").toInt() == active_base);
    }
}

} // namespace

HexPreviewWidget::HexPreviewWidget(QWidget* parent)
    : QAbstractScrollArea(parent) {
    setObjectName(QStringLiteral("HexPreview"));
    setFrameShape(QFrame::NoFrame);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void HexPreviewWidget::set_storage(std::span<const uint8_t> bytes) {
    m_reader = nullptr;
    m_bytes.assign(bytes.begin(), bytes.end());
}

void HexPreviewWidget::set_storage(std::vector<uint8_t> bytes) {
    m_reader = nullptr;
    m_bytes = std::move(bytes);
}

void HexPreviewWidget::set_storage(const cricodecs::io::reader& reader) {
    m_reader = &reader;
    m_bytes.clear();
}

void HexPreviewWidget::set_source(
    std::span<const uint8_t> bytes,
    std::string_view format) {
    set_storage(bytes);
    configure_source(format, infer_hex_patterns(format, source_size(), initial_pattern_bytes(format)));
}

void HexPreviewWidget::set_source(
    std::vector<uint8_t> bytes,
    std::string_view format) {
    set_storage(std::move(bytes));
    configure_source(format, infer_hex_patterns(format, source_size(), initial_pattern_bytes(format)));
}

void HexPreviewWidget::set_source(
    std::span<const uint8_t> bytes,
    const EntrySummary& entry) {
    set_storage(bytes);
    const auto format = entry_hex_format(entry);
    configure_source(
        format,
        infer_entry_hex_patterns(entry, source_size(), initial_pattern_bytes(format)));
}

void HexPreviewWidget::set_source(
    std::vector<uint8_t> bytes,
    const EntrySummary& entry) {
    set_storage(std::move(bytes));
    const auto format = entry_hex_format(entry);
    configure_source(
        format,
        infer_entry_hex_patterns(entry, source_size(), initial_pattern_bytes(format)));
}

void HexPreviewWidget::set_source(
    const cricodecs::io::reader& reader,
    std::string_view format) {
    set_storage(reader);
    configure_source(format, infer_hex_patterns(format, source_size(), initial_pattern_bytes(format)));
}

void HexPreviewWidget::set_source(
    const cricodecs::io::reader& reader,
    const LoadedDocument& document) {
    set_storage(reader);
    const auto format = document_format_id(document);
    configure_source(
        format,
        infer_document_hex_patterns(document, initial_pattern_bytes(format)));
}

void HexPreviewWidget::reset_lazy_state() {
    m_lazy_usm_chunks.clear();
    m_lazy_usm_scanned_until = 0;
    m_lazy_usm_valid = false;
    m_lazy_sbt_cues.clear();
    m_lazy_sbt_scanned_until = 0;
    m_lazy_sbt_valid = false;
    m_lazy_chunks.clear();
    m_lazy_chunk_scanned_until = 0;
    m_lazy_chunks_initialized = false;
    m_lazy_chunks_valid = false;
    m_lazy_cvm_initialized = false;
    m_lazy_cvm_pvd_offset = 0;
    m_lazy_cvm_pvd_valid = false;
}

void HexPreviewWidget::apply_patterns(HexPatternSet patterns) {
    std::erase_if(patterns.ranges, [](const HexPatternRange& pattern) {
        return pattern.size == 0;
    });
    std::erase_if(patterns.repeats, [](const HexPatternRepeat& pattern) {
        return pattern.size == 0 || pattern.stride == 0 || pattern.count == 0;
    });
    std::ranges::sort(patterns.ranges, {}, &HexPatternRange::offset);
    std::ranges::sort(patterns.repeats, {}, &HexPatternRepeat::offset);
    m_pattern_prefix_max_end.clear();
    m_pattern_prefix_max_end.reserve(patterns.ranges.size());
    uint64_t max_end = 0;
    for (const auto& pattern : patterns.ranges) {
        max_end = std::max(max_end, pattern_end(pattern));
        m_pattern_prefix_max_end.push_back(max_end);
    }
    m_patterns = std::move(patterns);
}

void HexPreviewWidget::configure_source(std::string_view format, HexPatternSet patterns) {
    m_lazy_format = lower_ascii(format);
    reset_lazy_state();
    apply_patterns(std::move(patterns));
    m_anchor.reset();
    m_cursor.reset();
    m_offset_row = 0;
    m_active_lane = Lane::Hex;
    m_dragging = false;
    m_pattern_status.clear();
    setToolTip({});
    update_scrollbar();
    verticalScrollBar()->setValue(0);
    viewport()->update();
}

void HexPreviewWidget::set_patterns_enabled(bool enabled) {
    if (m_patterns_enabled == enabled) {
        return;
    }
    m_patterns_enabled = enabled;
    viewport()->update();
}

void HexPreviewWidget::clear_bytes() {
    m_reader = nullptr;
    m_bytes.clear();
    configure_source({}, {});
}

int HexPreviewWidget::row_height() const {
    return QFontMetrics(font()).height() + 4;
}

void HexPreviewWidget::update_scrollbar() {
    const auto bytes = source_size();
    const auto rows = static_cast<int>((bytes + m_bytes_per_row - 1) / m_bytes_per_row);
    const auto visible_rows = std::max(1, (viewport()->height() - row_height() * 2) / std::max(1, row_height()));
    auto* bar = verticalScrollBar();
    bar->setRange(0, std::max(0, rows - visible_rows));
    bar->setPageStep(visible_rows);
}

size_t HexPreviewWidget::source_size() const {
    return source_bytes().size();
}

std::span<const uint8_t> HexPreviewWidget::source_bytes() const {
    return m_reader != nullptr ? m_reader->data() : std::span<const uint8_t>(m_bytes);
}

std::span<const uint8_t> HexPreviewWidget::initial_pattern_bytes(std::string_view format) const {
    const auto source = source_bytes();
    const auto first = [source](uint64_t size) {
        return source.first(static_cast<size_t>(std::min<uint64_t>(size, source.size())));
    };

    if (is_lazy_usm_format(format) || is_usm_chunk_magic(source)) {
        return first(0x20);
    }
    if (is_lazy_sbt_format(format)) {
        return {};
    }
    if (is_lazy_riff_format(format) || (has_magic(source, "RIFF") && source.size() >= 12)) {
        if (source.size() < 20) {
            return source;
        }
        const auto payload_size = cricodecs::io::read_le<uint32_t>(source.data() + 16);
        return first(20ull + payload_size + (payload_size & 1u));
    }
    if (is_lazy_aix_format(format) || has_magic(source, "AIXF")) {
        if (source.size() < 8) {
            return source;
        }
        return first(static_cast<uint64_t>(cricodecs::io::read_be<uint32_t>(source.data() + 4)) + 8u);
    }
    if (is_lazy_cvm_format(format) || has_magic(source, "CVMH")) {
        return first(0x1000);
    }
    return source;
}

size_t HexPreviewWidget::read_source(size_t offset, std::span<uint8_t> output) const {
    const auto source = source_bytes();
    if (output.empty() || offset >= source.size()) {
        return 0;
    }
    const auto count = std::min(output.size(), source.size() - offset);
    std::copy_n(source.data() + offset, count, output.data());
    return count;
}

HexPreviewWidget::Layout HexPreviewWidget::layout() const {
    const QFontMetrics metrics(font());
    Layout out;
    out.char_w = metrics.horizontalAdvance(QLatin1Char('0'));
    out.line_h = row_height();
    out.offset_w = out.char_w * 8;
    out.hex_x = out.margin + out.offset_w + out.char_w * 3;
    out.text_x = out.hex_x + out.char_w * (m_bytes_per_row * 3 + 3);
    out.header_h = out.line_h;
    out.footer_h = m_pattern_status.isEmpty() ? 0 : out.line_h;
    return out;
}

QRect HexPreviewWidget::pattern_toggle_rect() const {
    if (!has_pattern_source()) {
        return {};
    }
    const auto l = layout();
    const QFontMetrics metrics(font());
    const auto label = m_patterns_enabled ? QStringLiteral("Patterns") : QStringLiteral("Plain");
    const auto width = metrics.horizontalAdvance(label) + l.char_w * 3;
    return QRect(viewport()->width() - width - l.margin, 3, width, std::max(1, l.header_h - 6));
}

bool HexPreviewWidget::has_pattern_source() const {
    return !(m_patterns.ranges.empty() && m_patterns.repeats.empty()) ||
        is_lazy_usm_format(m_lazy_format) || is_lazy_sbt_format(m_lazy_format) ||
        is_lazy_riff_format(m_lazy_format) || is_lazy_aix_format(m_lazy_format) ||
        is_lazy_cvm_format(m_lazy_format);
}

std::optional<size_t> HexPreviewWidget::byte_at(const QPoint& pos, Lane* lane) const {
    if (lane != nullptr) {
        *lane = Lane::None;
    }
    const auto l = layout();
    const auto y = pos.y() - l.header_h;
    if (y < 0) {
        return std::nullopt;
    }
    const auto row = verticalScrollBar()->value() + y / l.line_h;
    if (row < 0) {
        return std::nullopt;
    }
    const auto row_start = static_cast<size_t>(row * m_bytes_per_row);
    const auto bytes = source_size();
    if (row_start >= bytes) {
        return std::nullopt;
    }
    if (pos.x() >= l.margin && pos.x() < l.hex_x - l.char_w * 2) {
        if (lane != nullptr) {
            *lane = Lane::Offset;
        }
        return row_start;
    }
    if (pos.x() >= l.hex_x && pos.x() < l.hex_x + l.char_w * m_bytes_per_row * 3) {
        const auto column = std::clamp((pos.x() - l.hex_x) / std::max(1, l.char_w * 3), 0, m_bytes_per_row - 1);
        const auto index = row_start + static_cast<size_t>(column);
        if (index < bytes) {
            if (lane != nullptr) {
                *lane = Lane::Hex;
            }
            return index;
        }
    }
    if (pos.x() >= l.text_x && pos.x() < l.text_x + l.char_w * m_bytes_per_row) {
        const auto column = std::clamp((pos.x() - l.text_x) / std::max(1, l.char_w), 0, m_bytes_per_row - 1);
        const auto index = row_start + static_cast<size_t>(column);
        if (index < bytes) {
            if (lane != nullptr) {
                *lane = Lane::Text;
            }
            return index;
        }
    }
    return std::nullopt;
}

bool HexPreviewWidget::selected(size_t index) const {
    if (!m_anchor || !m_cursor) {
        return false;
    }
    const auto first = std::min(*m_anchor, *m_cursor);
    const auto last = std::max(*m_anchor, *m_cursor);
    return index >= first && index <= last;
}

bool HexPreviewWidget::has_selection() const {
    return m_anchor.has_value() && m_cursor.has_value();
}

std::optional<HexPreviewWidget::ActivePattern> HexPreviewWidget::pattern_at(size_t index) const {
    if (!m_patterns_enabled) {
        return std::nullopt;
    }
    std::optional<ActivePattern> best;
    const auto consider = [&best](ActivePattern pattern) {
        if (!best || pattern.size <= best->size) {
            best = std::move(pattern);
        }
    };
    const auto range_begin = std::ranges::upper_bound(
        m_patterns.ranges,
        static_cast<uint64_t>(index),
        {},
        &HexPatternRange::offset
    );
    const auto first_candidate = std::ranges::upper_bound(
        m_pattern_prefix_max_end,
        static_cast<uint64_t>(index)
    );
    auto it = m_patterns.ranges.begin() + std::distance(m_pattern_prefix_max_end.begin(), first_candidate);
    for (; it != range_begin; ++it) {
        if (pattern_end(*it) <= index) {
            continue;
        }
        consider(ActivePattern{it->offset, it->size, it->label, it->color});
    }
    for (const auto& repeat : m_patterns.repeats) {
        if (index < repeat.offset) {
            continue;
        }
        const auto relative = static_cast<uint64_t>(index) - repeat.offset;
        const auto instance = relative / repeat.stride;
        const auto within_stride = relative % repeat.stride;
        if (instance >= repeat.count || within_stride >= repeat.size) {
            continue;
        }
        const auto instance_offset = repeat.offset + instance * repeat.stride;
        consider(ActivePattern{
            instance_offset,
            repeat.size,
            QStringLiteral("%1 %2").arg(repeat.label).arg(static_cast<qulonglong>(instance)),
            repeat.color
        });
        for (const auto& field : repeat.fields) {
            if (field.size == 0 || within_stride < field.offset || within_stride >= field.offset + field.size) {
                continue;
            }
            const auto field_offset = instance_offset + field.offset;
            auto label = QStringLiteral("%1 %2.%3")
                .arg(repeat.label)
                .arg(static_cast<qulonglong>(instance))
                .arg(field.name);
            const auto value = value_text(static_cast<size_t>(field_offset), static_cast<size_t>(field.size), field.value_kind);
            if (!value.isEmpty()) {
                label += QStringLiteral(" = %1").arg(value);
            }
            consider(ActivePattern{field_offset, field.size, std::move(label), field.color});
        }
    }
    if (auto lazy = lazy_usm_pattern_at(index)) {
        consider(*lazy);
    }
    if (auto lazy = lazy_sbt_pattern_at(index)) {
        consider(*lazy);
    }
    if (auto lazy = lazy_chunk_pattern_at(index)) {
        consider(*lazy);
    }
    if (auto lazy = lazy_cvm_pattern_at(index)) {
        consider(*lazy);
    }
    return best;
}

void HexPreviewWidget::ensure_lazy_usm_chunks_until(uint64_t target_end) const {
    if (!is_lazy_usm_format(m_lazy_format) || source_size() < 0x20) {
        return;
    }
    if (!m_lazy_usm_valid && !m_lazy_usm_chunks.empty()) {
        return;
    }
    if (m_lazy_usm_chunks.empty() && m_lazy_usm_scanned_until == 0) {
        m_lazy_usm_valid = true;
    }

    const auto total = static_cast<uint64_t>(source_size());
    std::array<uint8_t, 0x20> header{};
    while (m_lazy_usm_valid && m_lazy_usm_scanned_until < target_end &&
           m_lazy_usm_scanned_until + header.size() <= total) {
        if (read_source(static_cast<size_t>(m_lazy_usm_scanned_until), header) != header.size()) {
            m_lazy_usm_valid = false;
            break;
        }
        const std::span<const uint8_t> view(header);
        if (!is_usm_chunk_magic(view)) {
            m_lazy_usm_valid = false;
            break;
        }
        const auto chunk_size = cricodecs::io::read_be<uint32_t>(header.data() + 0x04);
        const auto packed_size = static_cast<uint64_t>(chunk_size) + 0x08u;
        if (packed_size < header.size() || packed_size > total - m_lazy_usm_scanned_until) {
            m_lazy_usm_valid = false;
            break;
        }
        m_lazy_usm_chunks.push_back(LazyUsmChunk{
            .offset = m_lazy_usm_scanned_until,
            .chunk_size = chunk_size,
            .payload_offset = cricodecs::io::read_be<uint16_t>(header.data() + 0x08),
            .channel = header[0x0C],
            .payload_type_and_flags = cricodecs::io::read_be<uint16_t>(header.data() + 0x0E),
            .header = header,
            .magic = magic_text(view.first(4))
        });
        m_lazy_usm_scanned_until += packed_size;
    }
}

void HexPreviewWidget::ensure_lazy_sbt_cues_until(uint64_t target_end) const {
    constexpr uint64_t header_size = 0x14;
    const auto total = static_cast<uint64_t>(source_size());
    if (!is_lazy_sbt_format(m_lazy_format) || total < header_size || m_lazy_sbt_scanned_until >= total) {
        return;
    }
    if (m_lazy_sbt_cues.empty() && m_lazy_sbt_scanned_until == 0) {
        m_lazy_sbt_valid = true;
    }

    std::array<uint8_t, header_size> header{};
    while (m_lazy_sbt_valid && m_lazy_sbt_scanned_until < target_end && m_lazy_sbt_scanned_until < total) {
        if (total - m_lazy_sbt_scanned_until < header.size() ||
            read_source(static_cast<size_t>(m_lazy_sbt_scanned_until), header) != header.size()) {
            m_lazy_sbt_valid = false;
            m_lazy_sbt_scanned_until = total;
            break;
        }

        const auto language_id = cricodecs::io::read_le<uint32_t>(header.data() + 0x00);
        const auto time_unit = cricodecs::io::read_le<uint32_t>(header.data() + 0x04);
        const auto start_time = cricodecs::io::read_le<uint32_t>(header.data() + 0x08);
        const auto duration = cricodecs::io::read_le<uint32_t>(header.data() + 0x0C);
        const auto text_size = cricodecs::io::read_le<uint32_t>(header.data() + 0x10);
        const auto record_size = header_size + static_cast<uint64_t>(text_size);
        if (time_unit == 0 || record_size > total - m_lazy_sbt_scanned_until) {
            m_lazy_sbt_valid = false;
            m_lazy_sbt_scanned_until = total;
            break;
        }

        uint32_t terminator_size = 0;
        std::array<uint8_t, 1> tail{};
        while (terminator_size < text_size) {
            const auto tail_offset = m_lazy_sbt_scanned_until + record_size - terminator_size - 1;
            if (read_source(static_cast<size_t>(tail_offset), tail) != 1 || tail[0] != 0) {
                break;
            }
            ++terminator_size;
        }
        m_lazy_sbt_cues.push_back(LazySbtCue{
            .offset = m_lazy_sbt_scanned_until,
            .language_id = language_id,
            .time_unit = time_unit,
            .start_time = start_time,
            .duration = duration,
            .text_size = text_size,
            .terminator_size = terminator_size,
        });
        m_lazy_sbt_scanned_until += record_size;
    }
}

std::optional<HexPreviewWidget::ActivePattern> HexPreviewWidget::lazy_sbt_pattern_at(size_t index) const {
    constexpr uint64_t header_size = 0x14;
    if (!m_patterns_enabled || !is_lazy_sbt_format(m_lazy_format)) {
        return std::nullopt;
    }
    ensure_lazy_sbt_cues_until(static_cast<uint64_t>(index) + 1u);
    const auto found = std::ranges::upper_bound(
        m_lazy_sbt_cues,
        static_cast<uint64_t>(index),
        {},
        &LazySbtCue::offset
    );
    if (found == m_lazy_sbt_cues.begin()) {
        return std::nullopt;
    }
    const auto cue_index = static_cast<uint64_t>(std::distance(m_lazy_sbt_cues.begin(), std::prev(found)));
    const auto& cue = *std::prev(found);
    const auto record_size = header_size + static_cast<uint64_t>(cue.text_size);
    const auto position = static_cast<uint64_t>(index);
    if (position >= cue.offset + record_size) {
        return std::nullopt;
    }

    const auto rel = position - cue.offset;
    const auto field = [&](uint64_t offset, uint64_t size, QString label, QColor color) -> std::optional<ActivePattern> {
        if (rel >= offset && rel < offset + size) {
            return ActivePattern{cue.offset + offset, size, std::move(label), color};
        }
        return std::nullopt;
    };
    if (auto out = field(0x00, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "cue %1.language_id = %2").arg(cue_index).arg(cue.language_id), lazy_usm_color(cue_index + 1))) return out;
    if (auto out = field(0x04, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "cue %1.time_unit = %2").arg(cue_index).arg(cue.time_unit), lazy_usm_color(cue_index + 2))) return out;
    if (auto out = field(0x08, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "cue %1.start_time = %2").arg(cue_index).arg(cue.start_time), lazy_usm_color(cue_index + 3))) return out;
    if (auto out = field(0x0C, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "cue %1.duration = %2").arg(cue_index).arg(cue.duration), lazy_usm_color(cue_index + 4))) return out;
    if (auto out = field(0x10, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "cue %1.text_size = %2").arg(cue_index).arg(cue.text_size), lazy_usm_color(cue_index + 5))) return out;

    const auto text_offset = cue.offset + header_size;
    const auto content_size = static_cast<uint64_t>(cue.text_size - cue.terminator_size);
    if (position >= text_offset && position < text_offset + content_size) {
        return ActivePattern{text_offset, content_size, QCoreApplication::translate("Editor.HexPreviewWidget", "cue %1.text UTF-8").arg(cue_index), lazy_usm_color(cue_index + 6)};
    }
    if (cue.terminator_size != 0 && position >= text_offset + content_size) {
        return ActivePattern{text_offset + content_size, cue.terminator_size, QCoreApplication::translate("Editor.HexPreviewWidget", "cue %1.text terminator").arg(cue_index), lazy_usm_color(cue_index + 7)};
    }
    return ActivePattern{cue.offset, record_size, QCoreApplication::translate("Editor.HexPreviewWidget", "SBT cue %1").arg(cue_index), lazy_usm_color(cue_index)};
}

void HexPreviewWidget::ensure_lazy_chunks_until(uint64_t target_end) const {
    const auto riff = is_lazy_riff_format(m_lazy_format);
    const auto aix = is_lazy_aix_format(m_lazy_format);
    const auto total = static_cast<uint64_t>(source_size());
    if ((!riff && !aix) || total < 12) {
        return;
    }
    if (!m_lazy_chunks_initialized) {
        m_lazy_chunks_initialized = true;
        m_lazy_chunks_valid = false;
        if (riff) {
            std::array<uint8_t, 12> header{};
            if (read_source(0, header) == header.size() && has_magic(header, "RIFF") && has_magic(std::span(header).subspan(8), "WAVE")) {
                m_lazy_chunk_scanned_until = 12;
                m_lazy_chunks_valid = true;
            }
        } else {
            std::array<uint8_t, 0x20> header{};
            if (read_source(0, header) == header.size() && has_magic(header, "AIXF")) {
                const auto data_offset = static_cast<uint64_t>(cricodecs::io::read_be<uint32_t>(header.data() + 0x04)) + 0x08u;
                if (data_offset < total) {
                    m_lazy_chunk_scanned_until = data_offset;
                    m_lazy_chunks_valid = true;
                }
            }
        }
    }

    while (m_lazy_chunks_valid && m_lazy_chunk_scanned_until < target_end && m_lazy_chunk_scanned_until < total) {
        LazyChunk chunk;
        chunk.offset = m_lazy_chunk_scanned_until;
        const auto header_size = riff ? size_t{8} : size_t{0x10};
        if (total - chunk.offset < header_size ||
            read_source(static_cast<size_t>(chunk.offset), std::span(chunk.header).first(header_size)) != header_size) {
            m_lazy_chunks_valid = false;
            break;
        }
        chunk.id = QString::fromLatin1(reinterpret_cast<const char*>(chunk.header.data()), 4);
        if (riff) {
            chunk.payload_size = cricodecs::io::read_le<uint32_t>(chunk.header.data() + 4);
            chunk.size = 8u + static_cast<uint64_t>(chunk.payload_size) + (chunk.payload_size & 1u);
        } else {
            if (chunk.id != QStringLiteral("AIXP") && chunk.id != QStringLiteral("AIXE")) {
                m_lazy_chunks_valid = false;
                break;
            }
            chunk.payload_size = cricodecs::io::read_be<uint32_t>(chunk.header.data() + 4);
            chunk.size = 8u + static_cast<uint64_t>(chunk.payload_size);
        }
        if (chunk.size < header_size || chunk.size > total - chunk.offset) {
            m_lazy_chunks_valid = false;
            break;
        }
        m_lazy_chunks.push_back(std::move(chunk));
        m_lazy_chunk_scanned_until += m_lazy_chunks.back().size;
    }
}

std::optional<HexPreviewWidget::ActivePattern> HexPreviewWidget::lazy_chunk_pattern_at(size_t index) const {
    if (!m_patterns_enabled || (!is_lazy_riff_format(m_lazy_format) && !is_lazy_aix_format(m_lazy_format))) {
        return std::nullopt;
    }
    ensure_lazy_chunks_until(static_cast<uint64_t>(index) + 1u);
    const auto found = std::ranges::upper_bound(m_lazy_chunks, static_cast<uint64_t>(index), {}, &LazyChunk::offset);
    if (found == m_lazy_chunks.begin()) {
        return std::nullopt;
    }
    const auto chunk_index = static_cast<uint64_t>(std::distance(m_lazy_chunks.begin(), std::prev(found)));
    const auto& chunk = *std::prev(found);
    const auto position = static_cast<uint64_t>(index);
    if (position >= chunk.offset + chunk.size) {
        return std::nullopt;
    }
    const auto rel = position - chunk.offset;
    if (rel < 4) {
        return ActivePattern{chunk.offset, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "%1 chunk %2.id = %3").arg(is_lazy_aix_format(m_lazy_format) ? QStringLiteral("AIX") : QStringLiteral("RIFF")).arg(chunk_index).arg(chunk.id), lazy_usm_color(chunk_index + 1)};
    }
    if (rel < 8) {
        return ActivePattern{chunk.offset + 4, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk %1.payload_size = %2").arg(chunk_index).arg(chunk.payload_size), lazy_usm_color(chunk_index + 2)};
    }
    if (is_lazy_aix_format(m_lazy_format) && chunk.id == QStringLiteral("AIXP") && rel < 0x10) {
        if (rel < 9) return ActivePattern{chunk.offset + 8, 1, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk %1.layer_index = %2").arg(chunk_index).arg(chunk.header[8]), lazy_usm_color(chunk_index + 3)};
        if (rel < 10) return ActivePattern{chunk.offset + 9, 1, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk %1.layer_count = %2").arg(chunk_index).arg(chunk.header[9]), lazy_usm_color(chunk_index + 4)};
        if (rel < 12) return ActivePattern{chunk.offset + 10, 2, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk %1.data_size = %2").arg(chunk_index).arg(cricodecs::io::read_be<uint16_t>(chunk.header.data() + 10)), lazy_usm_color(chunk_index + 5)};
        return ActivePattern{chunk.offset + 12, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk %1.sequence = %2").arg(chunk_index).arg(cricodecs::io::read_be<uint32_t>(chunk.header.data() + 12)), lazy_usm_color(chunk_index + 6)};
    }
    return ActivePattern{chunk.offset + 8, chunk.size - 8, QCoreApplication::translate("Editor.HexPreviewWidget", "%1 %2 payload").arg(chunk.id).arg(chunk_index), lazy_usm_color(chunk_index)};
}

std::optional<HexPreviewWidget::ActivePattern> HexPreviewWidget::lazy_cvm_pattern_at(size_t index) const {
    constexpr uint64_t sector_size = 0x800;
    if (!m_patterns_enabled || !is_lazy_cvm_format(m_lazy_format) || source_size() < 0x838) {
        return std::nullopt;
    }
    if (!m_lazy_cvm_initialized) {
        m_lazy_cvm_initialized = true;
        std::array<uint8_t, 8> fields{};
        if (read_source(0x81C, std::span(fields).first(4)) == 4 &&
            read_source(0x82C, std::span(fields).subspan(4, 4)) == 4) {
            const auto declared_sector_size = cricodecs::io::read_be<uint32_t>(fields.data());
            const auto iso_sector = cricodecs::io::read_be<uint32_t>(fields.data() + 4);
            if (declared_sector_size != 0) {
                m_lazy_cvm_pvd_offset = static_cast<uint64_t>(iso_sector + 16u) * declared_sector_size;
                std::array<uint8_t, 6> signature{};
                m_lazy_cvm_pvd_valid = m_lazy_cvm_pvd_offset + signature.size() <= source_size() &&
                    read_source(static_cast<size_t>(m_lazy_cvm_pvd_offset), signature) == signature.size() &&
                    signature[0] == 1 && has_magic(std::span(signature).subspan(1), "CD001");
            }
        }
    }
    const auto position = static_cast<uint64_t>(index);
    if (!m_lazy_cvm_pvd_valid || position < m_lazy_cvm_pvd_offset || position >= m_lazy_cvm_pvd_offset + sector_size) {
        return std::nullopt;
    }
    if (position == m_lazy_cvm_pvd_offset) {
        return ActivePattern{m_lazy_cvm_pvd_offset, 1, QCoreApplication::translate("Editor.HexPreviewWidget", "ISO PVD type = 1"), lazy_usm_color(1)};
    }
    if (position < m_lazy_cvm_pvd_offset + 6) {
        return ActivePattern{m_lazy_cvm_pvd_offset + 1, 5, QCoreApplication::translate("Editor.HexPreviewWidget", "ISO PVD identifier = CD001"), lazy_usm_color(2)};
    }
    return ActivePattern{m_lazy_cvm_pvd_offset, sector_size, QCoreApplication::translate("Editor.HexPreviewWidget", "ISO9660 primary volume descriptor"), lazy_usm_color(0)};
}

std::optional<HexPreviewWidget::ActivePattern> HexPreviewWidget::lazy_usm_pattern_at(size_t index) const {
    if (!m_patterns_enabled || !is_lazy_usm_format(m_lazy_format)) {
        return std::nullopt;
    }
    ensure_lazy_usm_chunks_until(static_cast<uint64_t>(index) + 1u);
    const auto found = std::ranges::upper_bound(
        m_lazy_usm_chunks,
        static_cast<uint64_t>(index),
        {},
        &LazyUsmChunk::offset
    );
    if (found == m_lazy_usm_chunks.begin()) {
        return std::nullopt;
    }
    const auto& chunk = *std::prev(found);
    const auto packed_size = static_cast<uint64_t>(chunk.chunk_size) + 0x08u;
    if (static_cast<uint64_t>(index) >= chunk.offset + packed_size) {
        return std::nullopt;
    }
    const auto header_end = chunk.offset + 0x20u;
    const auto payload_offset = chunk.offset + 0x08u + chunk.payload_offset;
    if (static_cast<uint64_t>(index) < header_end) {
        const auto rel = static_cast<uint64_t>(index) - chunk.offset;
        const auto field = [&](uint64_t offset, uint64_t size, QString label, QColor color) -> std::optional<ActivePattern> {
            if (rel >= offset && rel < offset + size) {
                return ActivePattern{chunk.offset + offset, size, std::move(label), color};
            }
            return std::nullopt;
        };
        if (auto out = field(0x00, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.magic = %1").arg(chunk.magic), lazy_usm_color(chunk.offset + 10))) {
            return out;
        }
        if (auto out = field(0x04, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.size = %1").arg(chunk.chunk_size), lazy_usm_color(chunk.offset + 11))) {
            return out;
        }
        if (auto out = field(0x08, 2, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.payload_offset = %1").arg(chunk.payload_offset), lazy_usm_color(chunk.offset + 12))) {
            return out;
        }
        if (auto out = field(0x0A, 2, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.padding = %1").arg(cricodecs::io::read_be<uint16_t>(chunk.header.data() + 0x0A)), lazy_usm_color(chunk.offset + 14))) {
            return out;
        }
        if (auto out = field(0x0C, 1, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.channel = %1").arg(chunk.channel), lazy_usm_color(chunk.offset + 15))) {
            return out;
        }
        if (auto out = field(0x0D, 1, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.reserved_0d = %1").arg(hex_byte(chunk.header[0x0D])), lazy_usm_color(chunk.offset + 16))) {
            return out;
        }
        if (auto out = field(0x0E, 2, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.payload_type_and_flags = %1").arg(hex_word(chunk.payload_type_and_flags)), lazy_usm_color(chunk.offset + 17))) {
            return out;
        }
        if (auto out = field(0x10, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.frame_time = %1").arg(cricodecs::io::read_be<uint32_t>(chunk.header.data() + 0x10)), lazy_usm_color(chunk.offset + 19))) {
            return out;
        }
        if (auto out = field(0x14, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.frame_rate = %1").arg(cricodecs::io::read_be<uint32_t>(chunk.header.data() + 0x14)), lazy_usm_color(chunk.offset + 20))) {
            return out;
        }
        if (auto out = field(0x18, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.reserved_18 = %1").arg(hex_word(cricodecs::io::read_be<uint32_t>(chunk.header.data() + 0x18))), lazy_usm_color(chunk.offset + 21))) {
            return out;
        }
        if (auto out = field(0x1C, 4, QCoreApplication::translate("Editor.HexPreviewWidget", "chunk.reserved_1c = %1").arg(hex_word(cricodecs::io::read_be<uint32_t>(chunk.header.data() + 0x1C))), lazy_usm_color(chunk.offset + 22))) {
            return out;
        }
        return ActivePattern{
            .offset = chunk.offset,
            .size = 0x20,
            .label = QCoreApplication::translate("Editor.HexPreviewWidget", "USM %1 header, channel %2, payload type 0x%3")
                .arg(chunk.magic)
                .arg(chunk.channel)
                .arg(chunk.payload_type_and_flags & 0x03u, 2, 16, QLatin1Char('0')).toUpper(),
            .color = lazy_usm_color(chunk.offset + 1)
        };
    }
    if (chunk.chunk_size > chunk.payload_offset && static_cast<uint64_t>(index) >= payload_offset) {
        return ActivePattern{
            .offset = payload_offset,
            .size = chunk.chunk_size - chunk.payload_offset,
            .label = QCoreApplication::translate("Editor.HexPreviewWidget", "USM %1 payload").arg(chunk.magic),
            .color = lazy_usm_color(chunk.offset + 2)
        };
    }
    return ActivePattern{
        .offset = chunk.offset,
        .size = packed_size,
        .label = QCoreApplication::translate("Editor.HexPreviewWidget", "USM chunk %1, size %2").arg(chunk.magic).arg(chunk.chunk_size),
        .color = lazy_usm_color(chunk.offset)
    };
}

QString HexPreviewWidget::pattern_status_text(const ActivePattern& pattern, size_t index) const {
    return QCoreApplication::translate("Editor.HexPreviewWidget", "%1  @ 0x%2  +0x%3  size %4")
        .arg(pattern.label)
        .arg(static_cast<qulonglong>(pattern.offset), 8, 16, QLatin1Char('0'))
        .arg(static_cast<qulonglong>(index - pattern.offset), 0, 16)
        .arg(static_cast<qulonglong>(pattern.size));
}

QString HexPreviewWidget::value_text(size_t offset, size_t size, HexPatternValueKind kind) const {
    if (kind == HexPatternValueKind::None || size == 0) {
        return {};
    }
    std::array<uint8_t, 16> small{};
    const auto count = read_source(offset, std::span<uint8_t>(small.data(), std::min(size, small.size())));
    if (count == 0) {
        return {};
    }
    const auto u16be = [&] {
        return static_cast<uint16_t>((static_cast<uint16_t>(small[0]) << 8) | small[1]);
    };
    const auto u16le = [&] {
        return static_cast<uint16_t>((static_cast<uint16_t>(small[1]) << 8) | small[0]);
    };
    const auto u32be = [&] {
        return (static_cast<uint32_t>(small[0]) << 24) | (static_cast<uint32_t>(small[1]) << 16) |
            (static_cast<uint32_t>(small[2]) << 8) | small[3];
    };
    const auto u32le = [&] {
        return (static_cast<uint32_t>(small[3]) << 24) | (static_cast<uint32_t>(small[2]) << 16) |
            (static_cast<uint32_t>(small[1]) << 8) | small[0];
    };
    switch (kind) {
        case HexPatternValueKind::U8:
            return QString::number(small[0]);
        case HexPatternValueKind::U16BE:
            return count >= 2 ? QString::number(u16be()) : QString{};
        case HexPatternValueKind::U16LE:
            return count >= 2 ? QString::number(u16le()) : QString{};
        case HexPatternValueKind::U32BE:
            return count >= 4 ? QString::number(u32be()) : QString{};
        case HexPatternValueKind::U32LE:
            return count >= 4 ? QString::number(u32le()) : QString{};
        case HexPatternValueKind::HexBytes: {
            QString out;
            for (size_t i = 0; i < count; ++i) {
                if (i != 0) {
                    out += QLatin1Char(' ');
                }
                out += QStringLiteral("%1").arg(small[i], 2, 16, QLatin1Char('0')).toUpper();
            }
            if (size > count) {
                out += QStringLiteral(" ...");
            }
            return out;
        }
        case HexPatternValueKind::None:
            break;
    }
    return {};
}

QString HexPreviewWidget::selected_hex() const {
    if (!m_anchor || !m_cursor) {
        return {};
    }
    const auto first = std::min(*m_anchor, *m_cursor);
    const auto bytes = source_size();
    if (bytes == 0) {
        return {};
    }
    const auto last = std::min(std::max(*m_anchor, *m_cursor), bytes - 1);
    QString out;
    std::vector<uint8_t> selected_bytes(last - first + 1);
    const auto count = read_source(first, selected_bytes);
    selected_bytes.resize(count);
    for (size_t index = 0; index < selected_bytes.size(); ++index) {
        if (index != 0) {
            out += QLatin1Char(' ');
        }
        out += QStringLiteral("%1").arg(selected_bytes[index], 2, 16, QLatin1Char('0')).toUpper();
    }
    return out;
}

QString HexPreviewWidget::selected_text() const {
    if (!m_anchor || !m_cursor) {
        return {};
    }
    const auto first = std::min(*m_anchor, *m_cursor);
    const auto bytes = source_size();
    if (bytes == 0) {
        return {};
    }
    const auto last = std::min(std::max(*m_anchor, *m_cursor), bytes - 1);
    QString out;
    std::vector<uint8_t> selected_bytes(last - first + 1);
    const auto count = read_source(first, selected_bytes);
    selected_bytes.resize(count);
    for (auto byte : selected_bytes) {
        out += text_char(byte);
    }
    return out;
}

QString HexPreviewWidget::selected_offsets() const {
    const auto offset = m_anchor ? *m_anchor : m_offset_row;
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(offset), 8, 16, QLatin1Char('0')).toUpper();
}

void HexPreviewWidget::clear_selection() {
    m_anchor.reset();
    m_cursor.reset();
    m_dragging = false;
    viewport()->update();
}

void HexPreviewWidget::set_pattern_status(QString text) {
    if (m_pattern_status == text) {
        return;
    }
    m_pattern_status = std::move(text);
    update_scrollbar();
    viewport()->update();
}

void HexPreviewWidget::copy_selection(Lane lane) {
    if (!has_selection() && lane != Lane::Offset) {
        return;
    }
    const auto text = lane == Lane::Offset ? selected_offsets() : (lane == Lane::Text ? selected_text() : selected_hex());
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void HexPreviewWidget::jump_to_offset(size_t offset) {
    const auto size = source_size();
    if (size == 0) {
        return;
    }
    offset = std::min(offset, size - 1);
    const auto row = offset / static_cast<size_t>(m_bytes_per_row);
    m_offset_row = row * static_cast<size_t>(m_bytes_per_row);
    m_anchor = offset;
    m_cursor = offset;
    m_active_lane = Lane::Hex;
    verticalScrollBar()->setValue(static_cast<int>(row));
    viewport()->update();
}

void HexPreviewWidget::show_go_to_offset_dialog() {
    const auto size = source_size();
    if (size == 0) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QCoreApplication::translate("Editor.HexPreviewWidget", "Go to Offset"));
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;

    auto* row = new QWidget(&dialog);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    auto* input = new QLineEdit(row);
    input->setText(offset_text(m_anchor.value_or(m_offset_row), 16));
    auto* base_selector = new QWidget(row);
    base_selector->setObjectName(QStringLiteral("OffsetBaseSelector"));
    auto* base_layout = new QHBoxLayout(base_selector);
    base_layout->setContentsMargins(0, 0, 0, 0);
    base_layout->setSpacing(0);
    int current_base = 16;
    for (const auto& item : {std::pair{QStringLiteral("hex"), 16}, std::pair{QStringLiteral("dec"), 10}}) {
        auto* button = new QToolButton(base_selector);
        button->setObjectName(QStringLiteral("OffsetBaseSegment"));
        button->setText(item.first);
        button->setProperty("baseValue", item.second);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setToolTip(QCoreApplication::translate("Editor.HexPreviewWidget", "Parse this offset as %1.").arg(item.first));
        button->setAccessibleName(QCoreApplication::translate("Editor.HexPreviewWidget", "Use %1 offset input").arg(item.first));
        base_layout->addWidget(button, 0);
        QObject::connect(button, &QToolButton::clicked, &dialog, [base_selector, input, &current_base, value = item.second] {
            const auto previous = current_base;
            if (previous != value) {
                if (auto parsed = parse_offset_text(input->text(), previous)) {
                    input->setText(offset_text(*parsed, value));
                    input->selectAll();
                }
                current_base = value;
            }
            sync_offset_base_buttons(base_selector, current_base);
        });
    }
    sync_offset_base_buttons(base_selector, current_base);
    row_layout->addWidget(input, 1);
    row_layout->addWidget(base_selector, 0);
    form->addRow(QCoreApplication::translate("Editor.HexPreviewWidget", "Offset"), row);
    root->addLayout(form);

    auto* limit = new QLabel(
        QCoreApplication::translate("Editor.HexPreviewWidget", "Range 0 to %1").arg(offset_text(static_cast<uint64_t>(size - 1), 16)),
        &dialog
    );
    root->addWidget(limit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(
        QCoreApplication::translate("Editor.HexPreviewWidget", "Jump"));
    root->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, input, &current_base, size] {
        const auto parsed = parse_offset_text(input->text(), current_base);
        if (!parsed) {
            QMessageBox::warning(&dialog, QCoreApplication::translate("Editor.HexPreviewWidget", "Invalid offset"), QCoreApplication::translate("Editor.HexPreviewWidget", "Enter a valid offset."));
            return;
        }
        if (*parsed >= size) {
            QMessageBox::warning(
                &dialog,
                QCoreApplication::translate("Editor.HexPreviewWidget", "Invalid offset"),
                QCoreApplication::translate("Editor.HexPreviewWidget", "Offset is past end of data.")
            );
            return;
        }
        jump_to_offset(static_cast<size_t>(*parsed));
        dialog.accept();
    });

    input->setFocus();
    input->selectAll();
    dialog.exec();
}

void HexPreviewWidget::contextMenuEvent(QContextMenuEvent* event) {
    const auto primary_lane = m_active_lane == Lane::Text ? Lane::Text : Lane::Hex;
    const auto alternate_lane = primary_lane == Lane::Text ? Lane::Hex : Lane::Text;

    QMenu menu(this);
    const auto copy_primary = menu.addAction(QCoreApplication::translate("Editor.HexPreviewWidget", "Copy"));
    copy_primary->setShortcut(QKeySequence::Copy);
    const auto copy_alternate = menu.addAction(primary_lane == Lane::Text ? QCoreApplication::translate("Editor.HexPreviewWidget", "Copy hex") : QCoreApplication::translate("Editor.HexPreviewWidget", "Copy text"));
    copy_alternate->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
    const auto copy_offset = menu.addAction(QCoreApplication::translate("Editor.HexPreviewWidget", "Copy offset"));
    copy_offset->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    const auto go_to_offset = menu.addAction(QCoreApplication::translate("Editor.HexPreviewWidget", "Go to offset..."));
    go_to_offset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    menu.addSeparator();
    const auto show_patterns = menu.addAction(QCoreApplication::translate("Editor.HexPreviewWidget", "Show patterns"));
    show_patterns->setCheckable(true);
    show_patterns->setChecked(m_patterns_enabled);
    show_patterns->setEnabled(has_pattern_source());

    copy_primary->setEnabled(has_selection());
    copy_alternate->setEnabled(has_selection());
    copy_offset->setEnabled(has_selection());
    go_to_offset->setEnabled(source_size() != 0);

    const auto chosen = menu.exec(event->globalPos());
    if (chosen == copy_primary) {
        copy_selection(primary_lane);
    } else if (chosen == copy_alternate) {
        copy_selection(alternate_lane);
    } else if (chosen == copy_offset) {
        copy_selection(Lane::Offset);
    } else if (chosen == go_to_offset) {
        show_go_to_offset_dialog();
    } else if (chosen == show_patterns) {
        set_patterns_enabled(show_patterns->isChecked());
    }
}

void HexPreviewWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        clear_selection();
        return;
    }
    if (event->key() == Qt::Key_C && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        copy_selection(Lane::Offset);
        return;
    }
    if (event->key() == Qt::Key_C && event->modifiers() == (Qt::ControlModifier | Qt::AltModifier)) {
        copy_selection(m_active_lane == Lane::Text ? Lane::Hex : Lane::Text);
        return;
    }
    if (event->key() == Qt::Key_G && event->modifiers() == Qt::ControlModifier) {
        show_go_to_offset_dialog();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copy_selection(m_active_lane);
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void HexPreviewWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && has_pattern_source() && pattern_toggle_rect().contains(event->pos())) {
        set_patterns_enabled(!m_patterns_enabled);
        return;
    }
    Lane lane = Lane::None;
    if (auto index = byte_at(event->pos(), &lane)) {
        m_active_lane = lane == Lane::None ? Lane::Hex : lane;
        m_offset_row = (*index / static_cast<size_t>(m_bytes_per_row)) * static_cast<size_t>(m_bytes_per_row);
        if (
            event->button() == Qt::LeftButton &&
            event->modifiers().testFlag(Qt::ShiftModifier) &&
            lane != Lane::Offset &&
            m_anchor.has_value()
        ) {
            m_cursor = *index;
            m_dragging = false;
            if (auto pattern = pattern_at(*index)) {
                set_pattern_status(pattern_status_text(*pattern, *index));
            }
            viewport()->update();
            return;
        }
        const auto end = lane == Lane::Offset
            ? std::min(m_offset_row + static_cast<size_t>(m_bytes_per_row) - 1, source_size() == 0 ? 0 : source_size() - 1)
            : *index;
        m_anchor = lane == Lane::Offset ? m_offset_row : *index;
        m_cursor = end;
        m_dragging = lane != Lane::Offset;
        if (auto pattern = pattern_at(*index)) {
            set_pattern_status(pattern_status_text(*pattern, *index));
        }
        viewport()->update();
    } else if (event->button() == Qt::LeftButton) {
        set_pattern_status({});
        clear_selection();
    }
}

void HexPreviewWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging) {
        if (auto index = byte_at(event->pos())) {
            if (auto pattern = pattern_at(*index)) {
                const auto text = pattern_status_text(*pattern, *index);
                setToolTip(text);
                set_pattern_status(text);
            } else {
                setToolTip({});
                set_pattern_status({});
            }
        } else {
            setToolTip({});
            set_pattern_status({});
        }
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    if (auto index = byte_at(event->pos())) {
        m_cursor = *index;
        viewport()->update();
    }
}

void HexPreviewWidget::mouseReleaseEvent(QMouseEvent*) {
    m_dragging = false;
}

void HexPreviewWidget::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    update_scrollbar();
}

void HexPreviewWidget::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().color(QPalette::Base));
    painter.setFont(font());

    const QFontMetrics metrics(font());
    const auto l = layout();
    const auto char_w = l.char_w;
    const auto line_h = l.line_h;
    const auto margin = l.margin;
    const auto hex_x = l.hex_x;
    const auto text_x = l.text_x;
    const auto header_h = l.header_h;
    const auto accent = palette().color(QPalette::Highlight);
    auto primary_fill = accent;
    auto mirror_fill = accent.lighter(130);
    mirror_fill.setAlpha(54);
    const auto text = palette().color(QPalette::Text);
    const auto muted = palette().color(QPalette::Mid);
    const auto soft = palette().color(QPalette::AlternateBase);
    const auto dark_base = is_dark_color(palette().color(QPalette::Base));
    const auto secondary_hex = dark_base ? accent.lighter(145) : accent.darker(115);

    painter.fillRect(QRect(0, 0, viewport()->width(), header_h), soft);
    painter.setPen(muted);
    painter.drawText(margin, metrics.ascent() + 2, QStringLiteral("OFFSET"));
    painter.drawText(hex_x, metrics.ascent() + 2, QStringLiteral("HEX"));
    painter.drawText(text_x, metrics.ascent() + 2, QStringLiteral("TEXT"));
    const auto has_patterns = has_pattern_source();
    if (has_patterns) {
        const auto toggle = pattern_toggle_rect();
        QColor fill = m_patterns_enabled ? accent : palette().color(QPalette::Button);
        fill.setAlpha(m_patterns_enabled ? 95 : 70);
        painter.setBrush(fill);
        painter.setPen(QPen(palette().color(QPalette::Midlight), 1));
        painter.drawRoundedRect(toggle, 4, 4);
        painter.setPen(m_patterns_enabled ? palette().color(QPalette::HighlightedText) : muted);
        painter.drawText(toggle, Qt::AlignCenter, m_patterns_enabled ? QStringLiteral("Patterns") : QStringLiteral("Plain"));
    }
    painter.setPen(QPen(palette().color(QPalette::Midlight), 1));
    painter.drawLine(hex_x - char_w * 2, 0, hex_x - char_w * 2, viewport()->height());
    painter.drawLine(text_x - char_w * 2, 0, text_x - char_w * 2, viewport()->height());

    const auto first_row = verticalScrollBar()->value();
    const auto visible_rows = std::max(1, (viewport()->height() - header_h - l.footer_h) / line_h);
    const auto bytes_size = source_size();
    const auto total_rows = static_cast<int>((bytes_size + m_bytes_per_row - 1) / m_bytes_per_row);
    std::vector<uint8_t> row_bytes(static_cast<size_t>(m_bytes_per_row));
    for (int row = first_row; row < std::min(total_rows, first_row + visible_rows); ++row) {
        const auto offset = static_cast<size_t>(row * m_bytes_per_row);
        const auto row_end = std::min(offset + static_cast<size_t>(m_bytes_per_row), bytes_size);
        const auto row_count = read_source(offset, std::span<uint8_t>(row_bytes.data(), row_end - offset));
        const auto y = header_h + (row - first_row) * line_h;
        if ((row & 1) != 0) {
            painter.fillRect(QRect(0, y, viewport()->width(), line_h), soft);
        }

        std::array<std::optional<ActivePattern>, 16> row_patterns{};
        if (m_patterns_enabled && has_patterns) {
            for (size_t index = offset; index < row_end; ++index) {
                row_patterns[index - offset] = pattern_at(index);
            }
            auto paint_pattern = [&](uint64_t first, uint64_t last, QColor color) {
                const auto start_column = static_cast<int>(first - offset);
                const auto end_column = static_cast<int>(last - offset);
                const QRect hex_rect(
                    hex_x + start_column * char_w * 3 - 1,
                    y + 1,
                    std::max(1, (end_column - start_column) * char_w * 3),
                    line_h - 2);
                auto fill = color;
                fill.setAlpha(dark_base ? 128 : 92);
                if ((row & 1) != 0) {
                    fill = dark_base ? fill.lighter(135) : fill.lighter(112);
                }
                painter.fillRect(hex_rect, fill);
                auto text_color = color;
                text_color.setAlpha(dark_base ? 118 : 72);
                const QRect text_rect(
                    text_x + start_column * char_w - 1,
                    y + 1,
                    std::max(1, (end_column - start_column) * char_w),
                    line_h - 2);
                painter.fillRect(text_rect, text_color);
                auto border = color.darker(145);
                border.setAlpha(185);
                painter.setPen(QPen(border, 1));
                painter.drawLine(hex_rect.topLeft(), hex_rect.topRight());
                painter.drawLine(hex_rect.bottomLeft(), hex_rect.bottomRight());
                painter.drawLine(text_rect.topLeft(), text_rect.topRight());
                painter.drawLine(text_rect.bottomLeft(), text_rect.bottomRight());
            };

            size_t run_start = offset;
            while (run_start < row_end) {
                const auto& pattern = row_patterns[run_start - offset];
                if (!pattern) {
                    ++run_start;
                    continue;
                }
                size_t run_end = run_start + 1;
                while (run_end < row_end) {
                    const auto& next = row_patterns[run_end - offset];
                    if (!next || next->offset != pattern->offset || next->size != pattern->size ||
                        next->label != pattern->label || next->color != pattern->color) {
                        break;
                    }
                    ++run_end;
                }
                paint_pattern(run_start, run_end, pattern->color);
                run_start = run_end;
            }
        }

        size_t selected_first = 0;
        size_t selected_last = 0;
        const auto has_row_selection = has_selection()
            && (selected_first = std::max(std::min(*m_anchor, *m_cursor), offset),
                selected_last = std::min(std::max(*m_anchor, *m_cursor), row_end - 1),
                selected_first <= selected_last);
        if (has_row_selection) {
            const auto start_column = static_cast<int>(selected_first - offset);
            const auto end_column = static_cast<int>(selected_last - offset);
            const auto hex_primary = m_active_lane != Lane::Text;
            const QRect hex_selection(
                hex_x + start_column * char_w * 3 - 2,
                y + 1,
                (end_column - start_column) * char_w * 3 + char_w * 2 + 4,
                line_h - 2);
            const QRect text_selection(
                text_x + start_column * char_w - 2,
                y + 1,
                (end_column - start_column + 1) * char_w + 4,
                line_h - 2);
            painter.fillRect(hex_selection, hex_primary ? primary_fill : mirror_fill);
            painter.fillRect(text_selection, hex_primary ? mirror_fill : primary_fill);
        }

        painter.setPen(muted);
        painter.drawText(margin, y + metrics.ascent() + 2,
            QStringLiteral("%1").arg(static_cast<qulonglong>(offset), 8, 16, QLatin1Char('0')).toUpper());

        for (size_t index = offset; index < offset + row_count; ++index) {
            const auto column = static_cast<int>(index - offset);
            const auto x = hex_x + column * char_w * 3;
            const auto byte = row_bytes[column];
            const auto pattern = (m_patterns_enabled && has_patterns)
                ? row_patterns[column]
                : std::optional<ActivePattern>{};
            painter.setPen(pattern ? readable_on(pattern->color, palette()) : (column >= 8 ? secondary_hex : text));
            if (selected(index) && m_active_lane != Lane::Text) {
                painter.setPen(palette().color(QPalette::HighlightedText));
            }
            painter.drawText(x, y + metrics.ascent() + 2,
                QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper());
            painter.setPen(selected(index) && m_active_lane == Lane::Text
                ? palette().color(QPalette::HighlightedText)
                : (pattern ? readable_on(pattern->color, palette()) : muted));
            painter.drawText(text_x + column * char_w, y + metrics.ascent() + 2, QString(text_char(byte)));
        }
    }

    painter.setPen(QPen(palette().color(QPalette::Midlight), 1));
    painter.drawLine(hex_x - char_w * 2, 0, hex_x - char_w * 2, viewport()->height());
    painter.drawLine(text_x - char_w * 2, 0, text_x - char_w * 2, viewport()->height());

    if (l.footer_h != 0) {
        const auto footer_y = viewport()->height() - line_h;
        painter.fillRect(QRect(0, footer_y, viewport()->width(), line_h), soft);
        painter.setPen(muted);
        painter.drawText(margin, footer_y + metrics.ascent() + 2, m_pattern_status);
    }
}

} // namespace cristudio
