// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"

#include <string_view>
namespace docenhance::contract {
// Finite decimals in the contract's grammar (spec/cli-contract.json "value_grammar"):
// -?(digits[.digits] | .digits)([eE][+-]?digits)?, so 1e2, 1e+2 and 1E+2 are the same value.
// Locale-independent; the whole input must be consumed and the value must lie in [low, high].
[[nodiscard]] core::Result<double> parse_finite(std::string_view input, double low, double high);
} // namespace docenhance::contract
