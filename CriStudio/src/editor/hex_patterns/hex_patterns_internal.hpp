#pragma once

#include "editor/hex_patterns.hpp"
#include "path_text.hpp"

#include <QColor>
#include <QString>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cristudio::hexpatterns {

constexpr uint64_t max_entry_patterns = 512;

[[nodiscard]] QColor tone(int index);
void add(std::vector<HexPatternRange>& out, uint64_t offset, uint64_t size, QString label, QColor color, uint64_t total_size);
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
);
[[nodiscard]] bool has(std::span<const uint8_t> bytes, size_t offset, std::string_view text);
[[nodiscard]] std::optional<uint64_t> decimal_prefix(std::string_view text);
[[nodiscard]] std::optional<uint64_t> byte_size_prefix(std::string_view text);
[[nodiscard]] std::optional<uint64_t> info_value(const LoadedDocument& document, std::string_view name);
[[nodiscard]] QString hex_value(uint64_t value, int width = 0);
[[nodiscard]] QString ascii_value(std::span<const uint8_t> bytes, size_t offset, size_t size);
void add_field(
    std::vector<HexPatternRange>& out,
    uint64_t offset,
    uint64_t size,
    QString name,
    QString value,
    int color_index,
    uint64_t total_size
);

void add_utf_patterns_at(
    std::vector<HexPatternRange>& out,
    uint64_t total_size,
    std::span<const uint8_t> prefix,
    uint64_t base_offset,
    QString label
);
void add_utf_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_hca_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_hca_repeats(std::vector<HexPatternRepeat>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_adx_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_adx_repeats(std::vector<HexPatternRepeat>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_riff_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_aix_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_cvm_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_usm_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_cpk_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix);
void add_afs_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size, std::span<const uint8_t> prefix);
void add_awb_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size, std::span<const uint8_t> prefix);
void add_acx_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size, std::span<const uint8_t> prefix);
void add_aax_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size, std::span<const uint8_t> prefix);
void add_sfd_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size);
void add_aix_document_patterns(std::vector<HexPatternRange>& out, const LoadedDocument& document);
void add_cpk_document_patterns(std::vector<HexPatternRange>& out, const LoadedDocument& document);
void add_entry_patterns(std::vector<HexPatternRange>& out, const LoadedDocument& document);

} // namespace cristudio::hexpatterns
