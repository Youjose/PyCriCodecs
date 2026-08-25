#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cristudio {

inline constexpr size_t file_sniff_prefix_size = 0x804;

[[nodiscard]] bool has_acx_header(std::span<const uint8_t> bytes);
[[nodiscard]] bool has_cvm_header(std::span<const uint8_t> bytes);
[[nodiscard]] bool has_cvm_header(const std::filesystem::path& path);
[[nodiscard]] bool has_hca_signature(std::span<const uint8_t> bytes);
[[nodiscard]] bool has_sfd_signature(std::span<const uint8_t> bytes);

[[nodiscard]] std::vector<std::string> sniff_format_order(
    std::span<const uint8_t> bytes,
    bool include_riff_wave = true
);
[[nodiscard]] std::vector<std::string> sniff_format_order(
    const std::filesystem::path& path,
    bool include_riff_wave = true
);
[[nodiscard]] std::vector<std::string> sniff_embedded_format_order(
    std::span<const uint8_t> bytes,
    std::string_view name,
    std::string_view type,
    std::string_view source_format,
    std::string_view nested_source_format
);
[[nodiscard]] std::vector<std::string> sniff_file_format_order(const std::filesystem::path& path);

} // namespace cristudio
