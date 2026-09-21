// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Differential fuzzing of contract::parse_pages against an independent reference implementation of
// the --pages grammar (docs/cli-target-reference.md), plus result invariants and a round trip.
#include "docenhance/contract/parse.hpp"
#include "docenhance/core/result.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace {
using docenhance::fuzz::fail;
using docenhance::fuzz::require;
constexpr std::size_t max_selection_length = 4096;
constexpr std::size_t max_expansion_limit = 4096;
// Slightly longer than the accepted maximum, so the length limit itself is exercised.
constexpr std::size_t max_input_length = max_selection_length + 16;

std::optional<std::uint32_t> reference_number(std::string_view text) {
    if (text.empty() || !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        value = (value * 10) + static_cast<std::uint64_t>(c - '0');
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
    }
    return value == 0 ? std::nullopt : std::optional<std::uint32_t>(value);
}

// Returns the sorted pages, or nullopt when the reference grammar rejects the input.
std::optional<std::vector<std::uint32_t>> reference(std::string_view input, std::size_t limit) {
    if (input.empty() || input.size() > max_selection_length || limit == 0) {
        return std::nullopt;
    }
    std::vector<std::uint32_t> pages;
    std::uint64_t total = 0;
    for (const auto part : std::views::split(input, ',')) {
        const std::string_view token(part.begin(), part.end());
        const auto hyphen = token.find('-');
        const auto first = reference_number(token.substr(0, hyphen));
        const auto last =
            hyphen == std::string_view::npos ? first : reference_number(token.substr(hyphen + 1));
        if (!first || !last || *last < *first) {
            return std::nullopt;
        }
        total += std::uint64_t{*last} - *first + 1;
        if (total > limit) {
            return std::nullopt;
        }
        for (std::uint64_t page = *first; page <= *last; ++page) {
            pages.push_back(static_cast<std::uint32_t>(page));
        }
    }
    std::ranges::sort(pages);
    if (std::ranges::adjacent_find(pages) != pages.end()) {
        return std::nullopt;
    }
    return pages;
}

std::string serialize(const std::vector<std::uint32_t>& pages) {
    std::string text;
    for (const auto page : pages) {
        text += (text.empty() ? "" : ",") + std::to_string(page);
    }
    return text;
}

void check_accepted(std::string_view input, std::size_t limit,
                    const docenhance::contract::PageSelection& selection) {
    if (input == "all") {
        require(selection.all && selection.pages.empty(), "'all' selects every page");
        return;
    }
    const auto expected = reference(input, limit);
    if (!expected) {
        fail("parse_pages accepted input the reference grammar rejects");
    }
    require(!selection.all && selection.pages == *expected, "parse_pages matches the reference");
    require(selection.pages.size() <= limit, "selection respects the expansion limit");
    const auto text = serialize(selection.pages);
    if (text.size() <= max_selection_length) {
        const auto again = docenhance::contract::parse_pages(text, limit);
        require(again.has_value() && again.value().pages == selection.pages,
                "canonical serialization round-trips");
    }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    docenhance::fuzz::FuzzInput input(std::span<const std::uint8_t>(data, size));
    const auto limit = std::size_t{input.integer<std::uint16_t>()} % (max_expansion_limit + 1);
    const auto text = input.rest(max_input_length);
    const auto result = docenhance::contract::parse_pages(text, limit);
    if (result.has_value()) {
        check_accepted(text, limit, result.value());
    } else {
        require(text != "all" && !reference(text, limit).has_value(),
                "parse_pages rejected input the reference grammar accepts");
        require(result.error().code == docenhance::core::ErrorCode::argument,
                "page-selection errors are argument errors");
        require(!result.error().message.empty(), "errors carry a message");
    }
    return 0;
}
