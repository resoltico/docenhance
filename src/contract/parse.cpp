// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/contract/parse.hpp"

#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
namespace docenhance::contract {
namespace {
constexpr std::size_t max_selection_length = 4096;
constexpr std::size_t max_decimal_length = 128;
struct PageRange {
    std::uint32_t first;
    std::uint32_t last;
};
[[nodiscard]] core::Error invalid(std::string text) {
    return {.code = core::ErrorCode::argument, .message = std::move(text)};
}
[[nodiscard]] core::Result<std::uint32_t> positive(std::string_view input) {
    if (input.empty() || !std::ranges::all_of(input, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::unexpected(invalid("Page numbers must be unsigned decimal integers"));
    }
    std::uint32_t number = 0;
    const char* const input_end = std::to_address(input.end());
    const auto [end, ec] = std::from_chars(std::to_address(input.begin()), input_end, number);
    if (ec != std::errc{} || end != input_end || number == 0) {
        return std::unexpected(invalid("Page number is zero or outside uint32 range"));
    }
    return number;
}
[[nodiscard]] core::Result<PageRange> parse_range(std::string_view token) {
    const auto hyphen = token.find('-');
    const auto first = positive(token.substr(0, hyphen));
    if (!first) {
        return std::unexpected(first.error());
    }
    if (hyphen == std::string_view::npos) {
        return PageRange{.first = first.value(), .last = first.value()};
    }
    const auto last = positive(token.substr(hyphen + 1));
    if (!last) {
        return std::unexpected(last.error());
    }
    if (last.value() < first.value()) {
        return std::unexpected(invalid("Reversed page range"));
    }
    return PageRange{.first = first.value(), .last = last.value()};
}
// The decimal grammar of spec/cli-contract.json: -?(digits[.digits] | .digits)([eE][+-]?digits)?
// and nothing else. A '+' is an exponent sign only; there is no leading '+', no whitespace of any
// kind, and no hexadecimal, infinity or NaN spelling. strtod alone is more permissive.
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
// Returns false, leaving pages unchanged, when the range would exceed the expansion limit.
[[nodiscard]] bool append_range(std::vector<std::uint32_t>& pages, PageRange range,
                                std::size_t expansion_limit) {
    const auto count = std::uint64_t{range.last} - std::uint64_t{range.first} + 1;
    if (count > expansion_limit ||
        pages.size() > expansion_limit - static_cast<std::size_t>(count)) {
        return false;
    }
    for (std::uint64_t page = range.first; page <= range.last; ++page) {
        pages.push_back(static_cast<std::uint32_t>(page));
    }
    return true;
}
} // namespace
core::Result<PageSelection> parse_pages(std::string_view input, std::size_t expansion_limit) {
    if (input == "all") {
        return PageSelection{.all = true, .pages = {}};
    }
    if (input.empty() || input.size() > max_selection_length || expansion_limit == 0) {
        return std::unexpected(invalid("Empty, overlong, or disabled page selection"));
    }
    PageSelection selection;
    std::size_t offset = 0;
    while (offset < input.size()) {
        const auto comma = input.find(',', offset);
        const auto last = comma == std::string_view::npos ? input.size() : comma;
        const auto range = parse_range(input.substr(offset, last - offset));
        if (!range) {
            return std::unexpected(range.error());
        }
        if (!append_range(selection.pages, range.value(), expansion_limit)) {
            return std::unexpected(invalid("Page selection exceeds expansion limit"));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        offset = comma + 1;
        if (offset == input.size()) {
            return std::unexpected(invalid("Trailing comma in page selection"));
        }
    }
    std::ranges::sort(selection.pages);
    if (std::ranges::adjacent_find(selection.pages) != selection.pages.end()) {
        return std::unexpected(invalid("Duplicate or overlapping page selection"));
    }
    return selection;
}
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
    const std::string null_terminated(input);
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(null_terminated.c_str(), &end);
    if (errno == ERANGE || end != std::to_address(null_terminated.cend()) ||
        !std::isfinite(value) || value < low || value > high) {
        return std::unexpected(
            invalid("Expected a finite, in-range decimal without surrounding whitespace"));
    }
    return value;
}
} // namespace docenhance::contract
