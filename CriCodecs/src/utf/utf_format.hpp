#pragma once

#include <cstdint>

namespace cricodecs::utf::detail {

// @UTF offsets below are stored relative to byte 0x08 of the table.
struct UtfHeader {
    uint32_t magic;
    uint32_t table_size;
    uint16_t version;
    uint16_t rows_offset;
    uint32_t strings_offset;
    uint32_t data_offset;
    uint32_t name_offset;
    uint16_t column_count;
    uint16_t row_width;
    uint32_t row_count;
};

} // namespace cricodecs::utf::detail
