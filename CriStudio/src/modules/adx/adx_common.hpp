#pragma once

#include "document/document_types.hpp"

#include "adx_codec.hpp"

#include <QString>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cristudio::modules::adx {

void apply_keys(cricodecs::adx::Adx& adx, const DecryptionKeys& keys);
[[nodiscard]] std::expected<std::vector<uint8_t>, QString> read_adx_source(
    const std::filesystem::path& path,
    std::string error_prefix);
[[nodiscard]] bool has_compatible_key(const cricodecs::adx::Adx& adx, const DecryptionKeys& keys);
[[nodiscard]] bool has_applicable_raw_key(const cricodecs::adx::Adx& adx, const DecryptionKeys& keys);
[[nodiscard]] QString payload_preview(QString label, std::span<const uint8_t> bytes);

} // namespace cristudio::modules::adx
