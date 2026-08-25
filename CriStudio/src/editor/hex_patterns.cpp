#include <QCoreApplication>
#include "editor/hex_patterns.hpp"
#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include <algorithm>
#include <string>

namespace cristudio {
namespace hexpatterns {

void add_known_patterns(HexPatternSet& out, std::string_view format, uint64_t total_size, std::span<const uint8_t> prefix) {
    add_utf_patterns(out.ranges, total_size, prefix);
    add_hca_patterns(out.ranges, total_size, prefix);
    add_hca_repeats(out.repeats, total_size, prefix);
    add_adx_patterns(out.ranges, total_size, prefix);
    add_adx_repeats(out.repeats, total_size, prefix);
    add_riff_patterns(out.ranges, total_size, prefix);
    add_aix_patterns(out.ranges, total_size, prefix);
    add_cvm_patterns(out.ranges, total_size, prefix);
    add_cpk_patterns(out.ranges, total_size, prefix);
    add_usm_patterns(out.ranges, total_size, prefix);

    add_afs_patterns(out.ranges, format, total_size, prefix);
    add_awb_patterns(out.ranges, format, total_size, prefix);
    add_acx_patterns(out.ranges, format, total_size, prefix);
    add_aax_patterns(out.ranges, format, total_size, prefix);
    add_sfd_patterns(out.ranges, format, total_size);
}

void add_entry_patterns(std::vector<HexPatternRange>& out, const LoadedDocument& document) {
    const auto lower = lower_ascii(document_format_id(document));
    if (lower.find("aix") != std::string::npos) {
        return;
    }
    uint64_t added = 0;
    for (const auto& entry : document.entries) {
        if (added >= max_entry_patterns) {
            break;
        }
        const auto offset = decimal_prefix(entry.offset);
        if (!offset) {
            continue;
        }
        const auto size = byte_size_prefix(entry.size).value_or(1);
        add(out, *offset, size, QCoreApplication::translate("Editor.HexPatterns", "entry: %1").arg(QString::fromStdString(entry.name)), tone(static_cast<int>(added) + 4), document.file_size);
        ++added;
    }
}

} // namespace hexpatterns

HexPatternSet infer_hex_patterns(
    std::string_view format,
    uint64_t total_size,
    std::span<const uint8_t> prefix
) {
    HexPatternSet out;
    out.ranges.reserve(16);
    hexpatterns::add_known_patterns(out, format, total_size, prefix);
    std::ranges::sort(out.ranges, {}, &HexPatternRange::offset);
    std::ranges::sort(out.repeats, {}, &HexPatternRepeat::offset);
    return out;
}

HexPatternSet infer_document_hex_patterns(const LoadedDocument& document, std::span<const uint8_t> prefix) {
    auto out = infer_hex_patterns(document_format_id(document), document.file_size, prefix);
    hexpatterns::add_aix_document_patterns(out.ranges, document);
    hexpatterns::add_cpk_document_patterns(out.ranges, document);
    hexpatterns::add_entry_patterns(out.ranges, document);
    std::ranges::sort(out.ranges, {}, &HexPatternRange::offset);
    return out;
}

HexPatternSet infer_entry_hex_patterns(
    const EntrySummary& entry,
    uint64_t total_size,
    std::span<const uint8_t> prefix
) {
    return infer_hex_patterns(entry_hex_format(entry), total_size, prefix);
}

std::string entry_hex_format(const EntrySummary& entry) {
    std::string format = entry.type.empty() ? entry.source_format : entry.type;
    if (!entry.nested_source_format.empty()) {
        format += " ";
        format += entry.nested_source_format;
    }
    return format;
}

} // namespace cristudio
