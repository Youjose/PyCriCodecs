#pragma once
#include "hca_format.hpp"

#include <array>

namespace cricodecs::hca::transform {

[[nodiscard]] std::array<float, HCA_SAMPLES_PER_SUBFRAME> dct4(
    const std::array<float, HCA_SAMPLES_PER_SUBFRAME>& input
);

} // namespace cricodecs::hca::transform
