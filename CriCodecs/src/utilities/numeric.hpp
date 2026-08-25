#pragma once
/**
 * @file numeric.hpp
 * @brief Checked numeric helpers shared by format modules.
 *
 * Project-local arithmetic and alignment helpers for CriCodecs parsers and
 * builders.
 */

#include <algorithm>
#include <cmath>
#include <concepts>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace cricodecs::util {

#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath > 0
#define CRICODECS_UTIL_CONSTEXPR_CMATH 1
#elif defined(__GNUC__) && !defined(__clang__)
// GCC accepts these cmath calls in constant evaluation before advertising the
// C++26 constexpr-cmath feature-test macro. Clang with the same headers does not.
#define CRICODECS_UTIL_CONSTEXPR_CMATH 1
#else
#define CRICODECS_UTIL_CONSTEXPR_CMATH 0
#endif

namespace detail {

template <std::floating_point T>
[[nodiscard]] constexpr bool is_finite(T value) noexcept {
    return value == value
        && value != std::numeric_limits<T>::infinity()
        && value != -std::numeric_limits<T>::infinity();
}

template <std::floating_point T>
[[nodiscard]] consteval T constexpr_cmath_domain_error() {
    throw "constexpr cmath fallback only supports finite values in its bounded approximation domain";
}

template <std::floating_point T>
[[nodiscard]] constexpr T abs_value(T value) noexcept {
    return value < static_cast<T>(0) ? -value : value;
}

template <std::floating_point T>
[[nodiscard]] consteval T wrap_angle(T angle) {
    constexpr T pi = static_cast<T>(3.14159265358979323846264338327950288L);
    constexpr T two_pi = pi * static_cast<T>(2);
    constexpr T max_supported_angle = [] {
        if constexpr (std::is_same_v<T, float>) {
            return static_cast<T>(256);
        } else {
            return static_cast<T>(1048576);
        }
    }();

    if (!is_finite(angle)) {
        return std::numeric_limits<T>::quiet_NaN();
    }
    if (abs_value(angle) > max_supported_angle) {
        return constexpr_cmath_domain_error<T>();
    }

    const T quotient = angle / two_pi;
    auto cycles = static_cast<int>(quotient);
    if (quotient > static_cast<T>(cycles) + static_cast<T>(0.5)) {
        ++cycles;
    } else if (quotient < static_cast<T>(cycles) - static_cast<T>(0.5)) {
        --cycles;
    }

    angle -= static_cast<T>(cycles) * two_pi;
    if (angle > pi) {
        angle -= two_pi;
    } else if (angle < -pi) {
        angle += two_pi;
    }
    return angle;
}

template <std::floating_point T>
[[nodiscard]] consteval T cosine_series_core(T x) noexcept {
    const T x2 = x * x;
    T sum = static_cast<T>(1);
    T term = static_cast<T>(1);
    for (int n = 1; n <= 12; ++n) {
        term *= -x2 / static_cast<T>((2 * n - 1) * (2 * n));
        sum += term;
    }
    return sum;
}

template <std::floating_point T>
[[nodiscard]] consteval T sine_series_core(T x) noexcept {
    const T x2 = x * x;
    T sum = x;
    T term = x;
    for (int n = 1; n <= 12; ++n) {
        term *= -x2 / static_cast<T>((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

template <std::floating_point T>
[[nodiscard]] consteval T cosine_series(T angle) {
    constexpr T pi = static_cast<T>(3.14159265358979323846264338327950288L);
    constexpr T half_pi = static_cast<T>(1.57079632679489661923132169163975144L);
    constexpr T quarter_pi = static_cast<T>(0.78539816339744830961566084581987572L);
    T x = abs_value(wrap_angle(angle));
    T sign = static_cast<T>(1);
    if (x > half_pi) {
        x = pi - x;
        sign = static_cast<T>(-1);
    }
    return sign * (x > quarter_pi ? sine_series_core(half_pi - x) : cosine_series_core(x));
}

template <std::floating_point T>
[[nodiscard]] consteval T sine_series(T angle) {
    constexpr T pi = static_cast<T>(3.14159265358979323846264338327950288L);
    constexpr T half_pi = static_cast<T>(1.57079632679489661923132169163975144L);
    constexpr T quarter_pi = static_cast<T>(0.78539816339744830961566084581987572L);
    T x = wrap_angle(angle);
    T sign = static_cast<T>(1);
    if (x < static_cast<T>(0)) {
        x = -x;
        sign = static_cast<T>(-1);
    }
    if (x > half_pi) {
        x = pi - x;
    }
    return sign * (x > quarter_pi ? cosine_series_core(half_pi - x) : sine_series_core(x));
}

template <std::floating_point T>
[[nodiscard]] consteval T exp_series(T value) noexcept {
    T sum = static_cast<T>(1);
    T term = static_cast<T>(1);
    for (int n = 1; n <= 24; ++n) {
        term *= value / static_cast<T>(n);
        sum += term;
    }
    return sum;
}

template <std::floating_point T>
[[nodiscard]] consteval T exp2_fallback(T exponent) {
    constexpr T ln2 = static_cast<T>(0.69314718055994530941723212145817657L);

    if (exponent != exponent) {
        return std::numeric_limits<T>::quiet_NaN();
    }
    if (exponent == std::numeric_limits<T>::infinity()) {
        return std::numeric_limits<T>::infinity();
    }
    if (exponent == -std::numeric_limits<T>::infinity()) {
        return static_cast<T>(0);
    }
    if (exponent > static_cast<T>(std::numeric_limits<T>::max_exponent)) {
        return std::numeric_limits<T>::infinity();
    }
    if (exponent < static_cast<T>(std::numeric_limits<T>::min_exponent - std::numeric_limits<T>::digits)) {
        return static_cast<T>(0);
    }

    int whole = static_cast<int>(exponent);
    if (static_cast<T>(whole) > exponent) {
        --whole;
    }
    const T fraction = exponent - static_cast<T>(whole);

    T result = exp_series(fraction * ln2);
    while (whole > 0) {
        result *= static_cast<T>(2);
        --whole;
    }
    while (whole < 0) {
        result *= static_cast<T>(0.5);
        ++whole;
    }
    return result;
}

} // namespace detail

template <std::floating_point T>
[[nodiscard]] constexpr T cos(T angle) {
    if consteval {
#if CRICODECS_UTIL_CONSTEXPR_CMATH
        return std::cos(angle);
#else
        return detail::cosine_series(angle);
#endif
    }
    return std::cos(angle);
}

template <std::floating_point T>
[[nodiscard]] constexpr T sin(T angle) {
    if consteval {
#if CRICODECS_UTIL_CONSTEXPR_CMATH
        return std::sin(angle);
#else
        return detail::sine_series(angle);
#endif
    }
    return std::sin(angle);
}

template <std::floating_point T>
[[nodiscard]] constexpr T exp2(T exponent) {
    if consteval {
#if CRICODECS_UTIL_CONSTEXPR_CMATH
        return std::exp2(exponent);
#else
        return detail::exp2_fallback(exponent);
#endif
    }
    return std::exp2(exponent);
}

template <std::floating_point T>
[[nodiscard]] constexpr T pow10(T exponent) {
    if consteval {
#if CRICODECS_UTIL_CONSTEXPR_CMATH
        return std::pow(static_cast<T>(10), exponent);
#else
        constexpr T log2_10 = static_cast<T>(3.32192809488736234787031942948939018L);
        return exp2(exponent * log2_10);
#endif
    }
    return std::pow(static_cast<T>(10), exponent);
}

template <std::totally_ordered T>
[[nodiscard]] constexpr T clamp(T value, T min, T max) noexcept {
    return std::clamp(value, min, max);
}

template <typename To, typename From>
    requires std::is_arithmetic_v<To> && std::is_arithmetic_v<From>
[[nodiscard]] constexpr To clamp_to(From value) noexcept {
    if constexpr (std::integral<To> && std::integral<From>) {
        if constexpr (std::is_signed_v<From> == std::is_signed_v<To>) {
            if constexpr (std::numeric_limits<From>::digits > std::numeric_limits<To>::digits) {
                if (value < static_cast<From>(std::numeric_limits<To>::lowest())) {
                    return std::numeric_limits<To>::lowest();
                }
                if (value > static_cast<From>(std::numeric_limits<To>::max())) {
                    return std::numeric_limits<To>::max();
                }
            }
        } else if constexpr (std::is_signed_v<From>) {
            if (value < 0) {
                return 0;
            }
            using UnsignedFrom = std::make_unsigned_t<From>;
            if constexpr (std::numeric_limits<UnsignedFrom>::digits > std::numeric_limits<To>::digits) {
                if (static_cast<UnsignedFrom>(value) > static_cast<UnsignedFrom>(std::numeric_limits<To>::max())) {
                    return std::numeric_limits<To>::max();
                }
            }
        } else {
            using UnsignedTo = std::make_unsigned_t<To>;
            if constexpr (std::numeric_limits<From>::digits > std::numeric_limits<To>::digits) {
                if (value > static_cast<From>(static_cast<UnsignedTo>(std::numeric_limits<To>::max()))) {
                    return std::numeric_limits<To>::max();
                }
            }
        }
        return static_cast<To>(value);
    } else {
        const auto min = static_cast<From>(std::numeric_limits<To>::lowest());
        const auto max = static_cast<From>(std::numeric_limits<To>::max());
        return static_cast<To>(clamp(value, min, max));
    }
}

template <std::unsigned_integral T, std::unsigned_integral U>
[[nodiscard]] constexpr std::common_type_t<T, U> divide_round_up(T value, U divisor) noexcept {
    using Result = std::common_type_t<T, U>;
    if (divisor == 0) {
        return 0;
    }
    const auto v = static_cast<Result>(value);
    const auto d = static_cast<Result>(divisor);
    return v == 0 ? 0 : ((v - 1) / d) + 1;
}

template <std::integral T, std::integral U>
[[nodiscard]] constexpr std::common_type_t<T, U> align_up(T value, U alignment) noexcept {
    using Result = std::common_type_t<T, U>;
    using UnsignedResult = std::make_unsigned_t<Result>;

    if (value < 0 || alignment <= 0) {
        return static_cast<Result>(value);
    }

    const auto a = static_cast<UnsignedResult>(alignment);
    if (a == 0) {
        return static_cast<Result>(value);
    }

    const auto v = static_cast<UnsignedResult>(value);
    const auto remainder = v % a;
    if (remainder == 0) {
        return static_cast<Result>(v);
    }
    const auto increment = a - remainder;
    if (v > std::numeric_limits<UnsignedResult>::max() - increment) {
        return std::numeric_limits<Result>::max();
    }
    return static_cast<Result>(v + increment);
}

template <std::integral T, std::integral U>
[[nodiscard]] std::expected<std::common_type_t<T, U>, std::string> align_up_checked(
    T value,
    U alignment,
    std::string_view context
) {
    using Result = std::common_type_t<T, U>;
    using UnsignedResult = std::make_unsigned_t<Result>;
    if (value < 0) {
        return std::unexpected(std::string(context) + ": value must be non-negative");
    }

    if (alignment == 0) {
        return std::unexpected(std::string(context) + ": alignment must be non-zero");
    }
    if (alignment < 0) {
        return std::unexpected(std::string(context) + ": alignment must be non-negative");
    }

    const auto v = static_cast<UnsignedResult>(value);
    const auto a = static_cast<UnsignedResult>(alignment);
    const auto remainder = v % a;
    const auto aligned = remainder == 0 ? v : v + (a - remainder);
    if (aligned < v) {
        return std::unexpected(std::string(context) + ": aligned value exceeds supported range");
    }
    if constexpr (std::is_signed_v<Result>) {
        if (aligned > static_cast<UnsignedResult>(std::numeric_limits<Result>::max())) {
            return std::unexpected(std::string(context) + ": aligned value exceeds supported range");
        }
    }
    return static_cast<Result>(aligned);
}

#undef CRICODECS_UTIL_CONSTEXPR_CMATH

} // namespace cricodecs::util
