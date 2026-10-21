// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_input_cases.hpp"
#include "binary_wide_cases.hpp"
#include "require.hpp"

#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE("Binary Q16 admission rejects direct and padding-only overlap before pending stop") {
    const auto before = assertion_counter().load();
    binary_input_alias_cases();
    REQUIRE(assertion_counter().load() > before);
}
TEST_CASE("Binary wide arithmetic retains carries borrows and nearest-even boundaries") {
    const auto before = assertion_counter().load();
    binary_wide_cases();
    REQUIRE(assertion_counter().load() > before);
}
} // namespace docenhance::tests
