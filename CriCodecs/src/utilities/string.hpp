#pragma once
/**
 * @file string.hpp
 * @brief Small ASCII/string helpers shared by format modules.
 *
 * Project-local text normalization helpers for CriCodecs parsers and builders.
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace cricodecs::util {

namespace detail {

[[nodiscard]] inline bool is_ascii_ws_or_nul(unsigned char ch) noexcept {
    return ch == '\0' || std::isspace(ch) != 0;
}

[[nodiscard]] inline bool ascii_eq_no_case(char lhs, char rhs) noexcept {
    return std::toupper(static_cast<unsigned char>(lhs)) == std::toupper(static_cast<unsigned char>(rhs));
}

} // namespace detail

[[nodiscard]] inline std::string trim_ascii(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() && detail::is_ascii_ws_or_nul(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin && detail::is_ascii_ws_or_nul(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] inline std::string trim_ascii(std::span<const uint8_t> bytes) {
    if (bytes.empty()) {
        return {};
    }
    return trim_ascii(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

[[nodiscard]] inline std::string uppercase_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

[[nodiscard]] inline std::string lowercase_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

[[nodiscard]] inline bool equals_ascii_case_insensitive(std::string_view lhs, std::string_view rhs) {
    return std::ranges::equal(lhs, rhs, detail::ascii_eq_no_case);
}

[[nodiscard]] inline bool starts_with_case_insensitive(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size()
        && std::ranges::equal(text.substr(0, prefix.size()), prefix, detail::ascii_eq_no_case);
}

} // namespace cricodecs::util
