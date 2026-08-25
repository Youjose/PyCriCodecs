/**
 * @file hca_transform.cpp
 * @brief Shared HCA DCT-IV transform implementation.
 */

#include "hca_transform.hpp"

#include <bit>
#include <cmath>
#include <numbers>
#include <utility>

namespace cricodecs::hca::transform {

namespace {

constexpr int DCT_STAGES = HCA_MDCT_BITS;
constexpr int COEFFICIENTS_PER_STAGE = HCA_SAMPLES_PER_SUBFRAME / 2;
constexpr float DCT4_SCALE = 0.125f; // sqrt(2.0 / 128.0)

struct DctTables {
    std::array<float, DCT_STAGES * COEFFICIENTS_PER_STAGE> sine{};
    std::array<float, DCT_STAGES * COEFFICIENTS_PER_STAGE> cosine{};
};

consteval DctTables generate_dct_tables() {
    DctTables tables{};

    for (int stage = 0, width = 2; stage < DCT_STAGES; ++stage, width <<= 1) {
        const int half = width / 2;
        const int group_count = HCA_SAMPLES_PER_SUBFRAME / width;
        const double scale = stage == 0
            ? static_cast<double>(DCT4_SCALE) / std::numbers::sqrt2_v<double>
            : 1.0;
        const size_t stage_offset = static_cast<size_t>(stage * COEFFICIENTS_PER_STAGE);

        for (int group = 0; group < group_count; ++group) {
            const float sign = (std::popcount(static_cast<unsigned>(group)) & 1) != 0
                ? 1.0f
                : -1.0f;
            for (int i = 0; i < half; ++i) {
                const double angle = std::numbers::pi_v<double> * static_cast<double>(2 * i + 1)
                    / static_cast<double>(4 * width);
                const size_t index = stage_offset + static_cast<size_t>(group * half + i);
                tables.sine[index] = static_cast<float>(std::sin(angle) * scale) * sign;
                tables.cosine[index] = static_cast<float>(std::cos(angle) * scale);
            }
        }
    }

    return tables;
}

inline constexpr auto DCT_TABLES = generate_dct_tables();

} // namespace

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("fp-contract=off")))
#endif
std::array<float, HCA_SAMPLES_PER_SUBFRAME> dct4(
    const std::array<float, HCA_SAMPLES_PER_SUBFRAME>& input
) {
    auto source_storage = input;
    std::array<float, HCA_SAMPLES_PER_SUBFRAME> destination_storage{};
    auto* source = &source_storage;
    auto* destination = &destination_storage;

    for (int half = HCA_SAMPLES_PER_SUBFRAME / 2, group_count = 1;
         half != 0;
         half >>= 1, group_count <<= 1) {
        for (int group = 0; group < group_count; ++group) {
            const int base = group * half * 2;
            for (int i = 0; i < half; ++i) {
                const float a = (*source)[static_cast<size_t>(base + i * 2)];
                const float b = (*source)[static_cast<size_t>(base + i * 2 + 1)];
                (*destination)[static_cast<size_t>(base + i)] = a + b;
                (*destination)[static_cast<size_t>(base + half + i)] = a - b;
            }
        }
        std::swap(source, destination);
    }

    for (int stage = 0, width = 2; stage < DCT_STAGES; ++stage, width <<= 1) {
        const int half = width / 2;
        const int group_count = HCA_SAMPLES_PER_SUBFRAME / width;
        const size_t stage_offset = static_cast<size_t>(stage * COEFFICIENTS_PER_STAGE);

        for (int group = 0; group < group_count; ++group) {
            const int base = group * width;
            for (int i = 0; i < half; ++i) {
                const size_t table_index = stage_offset + static_cast<size_t>(group * half + i);
                const float sine = DCT_TABLES.sine[table_index];
                const float cosine = DCT_TABLES.cosine[table_index];
                const float low = (*source)[static_cast<size_t>(base + i)];
                const float high = (*source)[static_cast<size_t>(base + half + i)];
                const float low_cosine = low * cosine;
                const float high_sine = high * sine;
                const float low_sine = low * sine;
                const float high_cosine = high * cosine;

                (*destination)[static_cast<size_t>(base + i)] = low_cosine - high_sine;
                (*destination)[static_cast<size_t>(base + width - 1 - i)] = low_sine + high_cosine;
            }
        }
        std::swap(source, destination);
    }

    return *source;
}

} // namespace cricodecs::hca::transform
