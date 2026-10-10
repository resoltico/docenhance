// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/contract/parse.hpp"

#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ios>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
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
namespace {
std::string quoted_value(std::string_view value) {
    constexpr std::size_t display_limit = 128;
    if (value.size() > display_limit || !core::valid_utf8(value)) {
        return "value omitted (overlong or invalid UTF-8)";
    }
    std::string text = "value \"";
    constexpr std::string_view hex = "0123456789abcdef";
    constexpr unsigned control_end = 32;
    constexpr unsigned delete_code = 127;
    constexpr unsigned hexadecimal_base = 16;
    for (const char byte : value) {
        const auto code = static_cast<unsigned char>(byte);
        if (code < control_end || code == delete_code) {
            text += "\\x";
            text += hex.at(code / hexadecimal_base);
            text += hex.at(code % hexadecimal_base);
        } else {
            if (byte == '\\' || byte == '"') {
                text += '\\';
            }
            text += byte;
        }
    }
    text += '"';
    return text;
}
} // namespace
core::Error option_error(OptionDiagnostic diagnostic) {
    std::string message{diagnostic.name};
    message += " " + quoted_value(diagnostic.value) + ": ";
    message += diagnostic.reason;
    for (const auto& option : option_catalog) {
        if (option.name == diagnostic.name) {
            message += "; domain/default: ";
            message += option.domain;
            break;
        }
    }
    return invalid(std::move(message));
}
core::Result<double> parse_decimal_option(std::string_view name, std::string_view input, double low,
                                          double high) {
    auto result = parse_finite(input, low, high);
    if (!result && result.error().code == core::ErrorCode::argument) {
        return std::unexpected(
            option_error({.name = name, .value = input, .reason = result.error().message}));
    }
    return result;
}
core::Result<std::uint32_t> parse_integer_option(std::string_view name, std::string_view input,
                                                 std::uint32_t low, std::uint32_t high, bool odd) {
    std::uint32_t value{};
    if (input.empty() || !std::ranges::all_of(input, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::unexpected(
            option_error({.name = name, .value = input, .reason = "Expected decimal digits only"}));
    }
    const auto parsed =
        std::from_chars(std::to_address(input.begin()), std::to_address(input.end()), value);
    if (parsed.ec != std::errc{} || parsed.ptr != std::to_address(input.end()) || value < low ||
        value > high || (odd && value % 2 == 0)) {
        return std::unexpected(option_error(
            {.name = name, .value = input, .reason = "Integer outside the permitted domain"}));
    }
    return value;
}
} // namespace docenhance::contract
