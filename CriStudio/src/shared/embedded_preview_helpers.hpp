#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <span>

namespace cristudio {

[[nodiscard]] bool is_supported_image_payload(std::span<const uint8_t> bytes);
[[nodiscard]] QString hex_preview(std::span<const uint8_t> bytes, size_t max_bytes = 4096);

} // namespace cristudio
