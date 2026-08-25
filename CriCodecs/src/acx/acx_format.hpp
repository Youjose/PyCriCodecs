#pragma once

#include <cstdint>

namespace cricodecs::acx::detail {

struct AcxHeader {
    uint32_t marker;
    uint32_t entry_count;
};

struct AcxRange {
    uint32_t offset;
    uint32_t size;
};

} // namespace cricodecs::acx::detail
