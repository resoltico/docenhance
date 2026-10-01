// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/contract/parse.hpp"

#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <ios>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
namespace docenhance::contract {
namespace {
constexpr std::size_t max_decimal_length = 128;
[[nodiscard]] core::Error invalid(std::string text) {
    return {.code = core::ErrorCode::argument, .message = std::move(text)};
}
// The decimal grammar of spec/cli-contract.json: -?(digits[.digits] | .digits)([eE][+-]?digits)?
// and nothing else. A '+' is an exponent sign only; there is no leading '+', no whitespace of any
// kind, and no hexadecimal, infinity or NaN spelling. Conversion follows this grammar check.
[[nodiscard]] bool is_decimal(std::string_view text) {
    std::size_t index = 0;
    const auto peek = [&text, &index] { return index < text.size() ? text.at(index) : '\0'; };
    const auto digits = [&peek, &index] {
        const auto start = index;
        while (peek() >= '0' && peek() <= '9') {
            ++index;
        }
        return index - start;
    };
    if (peek() == '-') {
        ++index;
    }
    auto mantissa_digits = digits();
    if (peek() == '.') {
        ++index;
        mantissa_digits += digits();
    }
    if (mantissa_digits == 0) {
        return false;
    }
    if (peek() == 'e' || peek() == 'E') {
        ++index;
        if (peek() == '-' || peek() == '+') {
            ++index;
        }
        if (digits() == 0) {
            return false;
        }
    }
    return index == text.size();
}
} // namespace
core::Result<double> parse_finite(std::string_view input, double low, double high) {
    if (!std::isfinite(low) || !std::isfinite(high) || low > high) {
        return std::unexpected(
            core::Error{.code = core::ErrorCode::invariant, .message = "Invalid parser bounds"});
    }
    if (input.empty() || input.size() > max_decimal_length) {
        return std::unexpected(invalid("Empty or overlong decimal"));
    }
    if (!is_decimal(input)) {
        return std::unexpected(
            invalid("Expected a finite, in-range decimal without surrounding whitespace"));
    }
    // Select the grammar's decimal point explicitly, independent of the embedding process locale.
    std::istringstream decimal{std::string(input)};
    decimal.imbue(std::locale::classic());
    // Backend exceptions retain their failure kind at the application/CLI boundary.
    decimal.exceptions(std::ios::badbit);
    double value = 0;
    decimal >> value;
    const auto mantissa = input.substr(0, input.find_first_of("eE"));
    const bool nonzero = std::ranges::any_of(mantissa, [](char c) { return c >= '1' && c <= '9'; });
    if (decimal.fail() || !decimal.eof() || !std::isfinite(value) ||
        std::fpclassify(value) == FP_SUBNORMAL || (value == 0 && nonzero) || value < low ||
        value > high) {
        return std::unexpected(
            invalid("Expected a finite, in-range decimal without surrounding whitespace"));
    }
    return value;
}
} // namespace docenhance::contract
