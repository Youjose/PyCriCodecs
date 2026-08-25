#pragma once

#include "editor/hex_patterns.hpp"

#include <QAbstractScrollArea>
#include <QRect>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cricodecs::io {
class reader;
}

namespace cristudio {

class HexPreviewWidget final : public QAbstractScrollArea {
public:
    explicit HexPreviewWidget(QWidget* parent = nullptr);

    void set_source(
        std::span<const uint8_t> bytes,
        std::string_view format = {});
    void set_source(
        std::vector<uint8_t> bytes,
        std::string_view format = {});
    void set_source(
        std::span<const uint8_t> bytes,
        const EntrySummary& entry);
    void set_source(
        std::vector<uint8_t> bytes,
        const EntrySummary& entry);
    void set_source(
        const cricodecs::io::reader& reader,
        std::string_view format = {});
    void set_source(
        const cricodecs::io::reader& reader,
        const LoadedDocument& document);
    void set_patterns_enabled(bool enabled);
    [[nodiscard]] bool patterns_enabled() const noexcept { return m_patterns_enabled; }
    void clear_bytes();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class Lane : uint8_t {
        None,
        Offset,
        Hex,
        Text
    };

    struct Layout {
        int char_w = 0;
        int line_h = 0;
        int margin = 10;
        int offset_w = 0;
        int hex_x = 0;
        int text_x = 0;
        int header_h = 0;
        int footer_h = 0;
    };

    [[nodiscard]] Layout layout() const;
    [[nodiscard]] QRect pattern_toggle_rect() const;
    [[nodiscard]] bool has_pattern_source() const;
    [[nodiscard]] size_t source_size() const;
    [[nodiscard]] std::span<const uint8_t> source_bytes() const;
    [[nodiscard]] std::span<const uint8_t> initial_pattern_bytes(std::string_view format) const;
    [[nodiscard]] size_t read_source(size_t offset, std::span<uint8_t> output) const;
    void set_storage(std::span<const uint8_t> bytes);
    void set_storage(std::vector<uint8_t> bytes);
    void set_storage(const cricodecs::io::reader& reader);
    [[nodiscard]] std::optional<size_t> byte_at(const QPoint& pos, Lane* lane = nullptr) const;
    [[nodiscard]] bool selected(size_t index) const;
    [[nodiscard]] bool has_selection() const;
    struct ActivePattern {
        uint64_t offset = 0;
        uint64_t size = 0;
        QString label;
        QColor color;
    };
    struct LazyUsmChunk {
        uint64_t offset = 0;
        uint32_t chunk_size = 0;
        uint16_t payload_offset = 0;
        uint8_t channel = 0;
        uint16_t payload_type_and_flags = 0;
        std::array<uint8_t, 0x20> header{};
        QString magic;
    };
    struct LazySbtCue {
        uint64_t offset = 0;
        uint32_t language_id = 0;
        uint32_t time_unit = 0;
        uint32_t start_time = 0;
        uint32_t duration = 0;
        uint32_t text_size = 0;
        uint32_t terminator_size = 0;
    };
    struct LazyChunk {
        uint64_t offset = 0;
        uint64_t size = 0;
        uint32_t payload_size = 0;
        std::array<uint8_t, 0x18> header{};
        QString id;
    };

    [[nodiscard]] std::optional<ActivePattern> pattern_at(size_t index) const;
    [[nodiscard]] std::optional<ActivePattern> lazy_usm_pattern_at(size_t index) const;
    [[nodiscard]] std::optional<ActivePattern> lazy_sbt_pattern_at(size_t index) const;
    [[nodiscard]] std::optional<ActivePattern> lazy_chunk_pattern_at(size_t index) const;
    [[nodiscard]] std::optional<ActivePattern> lazy_cvm_pattern_at(size_t index) const;
    void ensure_lazy_usm_chunks_until(uint64_t target_end) const;
    void ensure_lazy_sbt_cues_until(uint64_t target_end) const;
    void ensure_lazy_chunks_until(uint64_t target_end) const;
    [[nodiscard]] QString pattern_status_text(const ActivePattern& pattern, size_t index) const;
    [[nodiscard]] QString value_text(size_t offset, size_t size, HexPatternValueKind kind) const;
    [[nodiscard]] QString selected_hex() const;
    [[nodiscard]] QString selected_text() const;
    [[nodiscard]] QString selected_offsets() const;
    void clear_selection();
    void copy_selection(Lane lane);
    void jump_to_offset(size_t offset);
    void show_go_to_offset_dialog();
    void configure_source(std::string_view format, HexPatternSet patterns);
    void reset_lazy_state();
    void apply_patterns(HexPatternSet patterns);
    void set_pattern_status(QString text);
    void update_scrollbar();
    [[nodiscard]] int row_height() const;

    std::vector<uint8_t> m_bytes;
    HexPatternSet m_patterns;
    std::vector<uint64_t> m_pattern_prefix_max_end;
    const cricodecs::io::reader* m_reader = nullptr;
    std::string m_lazy_format;
    mutable std::vector<LazyUsmChunk> m_lazy_usm_chunks;
    mutable uint64_t m_lazy_usm_scanned_until = 0;
    mutable bool m_lazy_usm_valid = false;
    mutable std::vector<LazySbtCue> m_lazy_sbt_cues;
    mutable uint64_t m_lazy_sbt_scanned_until = 0;
    mutable bool m_lazy_sbt_valid = false;
    mutable std::vector<LazyChunk> m_lazy_chunks;
    mutable uint64_t m_lazy_chunk_scanned_until = 0;
    mutable bool m_lazy_chunks_initialized = false;
    mutable bool m_lazy_chunks_valid = false;
    mutable bool m_lazy_cvm_initialized = false;
    mutable uint64_t m_lazy_cvm_pvd_offset = 0;
    mutable bool m_lazy_cvm_pvd_valid = false;
    int m_bytes_per_row = 16;
    std::optional<size_t> m_anchor;
    std::optional<size_t> m_cursor;
    size_t m_offset_row = 0;
    Lane m_active_lane = Lane::Hex;
    bool m_dragging = false;
    bool m_patterns_enabled = true;
    QString m_pattern_status;
};

} // namespace cristudio
