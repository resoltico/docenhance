// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Fuzzing of contract::parse_finite. Acceptance must match an independent grammar (a regular
// expression, not the parser's scanner); accepted values must be finite, in range and exactly the
// correctly rounded conversion; invalid bounds are invariant errors, never argument errors.
#include "docenhance/contract/parse.hpp"
#include "docenhance/core/result.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <regex>
#include <span>
#include <string>
namespace {
using docenhance::core::ErrorCode;
using docenhance::fuzz::require;
constexpr std::size_t max_decimal_length = 128;
constexpr std::size_t max_input_length = max_decimal_length + 16;
struct Bounds {
    double low;
    double high;
};
// Bounds used by real options, plus degenerate and invalid ones.
constexpr auto option_bounds = std::to_array<Bounds>({
    {.low = -1000.0, .high = 1000.0},
    {.low = 0.0, .high = 1.0},
    {.low = 1e-8, .high = 1e-3},
    {.low = -180.0, .high = 180.0},
    {.low = 0.01, .high = 0.15},
    {.low = 0.0, .high = 0.0},
    {.low = -std::numeric_limits<double>::max(), .high = std::numeric_limits<double>::max()},
    {.low = 1.0, .high = 0.0},
});

bool reference_grammar(const std::string& text) {
    static const std::regex decimal(R"(-?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][-+]?[0-9]+)?)");
    return !text.empty() && text.size() <= max_decimal_length && std::regex_match(text, decimal);
}

Bounds choose_bounds(docenhance::fuzz::FuzzInput& input) {
    const auto selector = input.byte();
    if (selector < option_bounds.size()) {
        return option_bounds.at(selector);
    }
    return {.low = input.any_double(), .high = input.any_double()};
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    docenhance::fuzz::FuzzInput input(std::span<const std::uint8_t>(data, size));
    const auto bounds = choose_bounds(input);
    const auto text = input.rest(max_input_length);
    const auto result = docenhance::contract::parse_finite(text, bounds.low, bounds.high);
    if (!std::isfinite(bounds.low) || !std::isfinite(bounds.high) || bounds.low > bounds.high) {
        require(!result.has_value() && result.error().code == ErrorCode::invariant,
                "invalid bounds are an invariant error");
        return 0;
    }
    if (!reference_grammar(text)) {
        require(!result.has_value(), "parse_finite accepted text outside the decimal grammar");
        require(result.error().code == ErrorCode::argument,
                "malformed decimals are argument errors");
        return 0;
    }
    errno = 0;
    const double expected = std::strtod(text.c_str(), nullptr);
    const bool in_range = errno != ERANGE && std::isfinite(expected) && expected >= bounds.low &&
                          expected <= bounds.high;
    require(result.has_value() == in_range,
            "grammatical decimals are accepted exactly when in range");
    if (result.has_value()) {
        require(std::bit_cast<std::uint64_t>(result.value()) ==
                    std::bit_cast<std::uint64_t>(expected),
                "the accepted value is the correctly rounded conversion");
    } else {
        require(result.error().code == ErrorCode::argument,
                "out-of-range decimals are argument errors");
    }
    return 0;
}
