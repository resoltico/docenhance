// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"

#include <cstdint>
#include <string_view>
namespace docenhance::contract {
// Finite decimals in the contract's grammar (spec/cli-contract.json "value_grammar"):
// -?(digits[.digits] | .digits)([eE][+-]?digits)?, so 1e2, 1e+2 and 1E+2 are the same value.
// Locale-independent; the whole input must be consumed and the value must lie in [low, high].
[[nodiscard]] core::Result<double> parse_finite(std::string_view input, double low, double high);
// Diagnostics use reviewed domains; these helpers still parse against explicit typed bounds.
struct OptionDiagnostic {
    std::string_view name;
    std::string_view value;
    std::string_view reason;
};
[[nodiscard]] core::Error option_error(OptionDiagnostic diagnostic);
[[nodiscard]] core::Result<double>
parse_decimal_option(std::string_view name, std::string_view input, double low, double high);
[[nodiscard]] core::Result<std::uint32_t>
parse_integer_option(std::string_view name, std::string_view input, std::uint32_t low,
                     std::uint32_t high, bool odd = false);
} // namespace docenhance::contract
