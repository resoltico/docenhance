// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
namespace docenhance::contract {
struct PageSelection {
    bool all = false;
    std::vector<std::uint32_t> pages;
};
inline constexpr std::size_t default_page_expansion_limit = 1'000'000;
// Page existence is checked later against decoded top-level IFD count.
[[nodiscard]] core::Result<PageSelection>
parse_pages(std::string_view input, std::size_t expansion_limit = default_page_expansion_limit);
// Finite decimals in the contract's grammar (spec/cli-contract.json "value_grammar"):
// -?(digits[.digits] | .digits)([eE][+-]?digits)?, so 1e2, 1e+2 and 1E+2 are the same value.
// Locale-independent; the whole input must be consumed and the value must lie in [low, high].
[[nodiscard]] core::Result<double> parse_finite(std::string_view input, double low, double high);
} // namespace docenhance::contract
