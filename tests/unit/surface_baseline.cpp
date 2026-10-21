// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "require.hpp"
#include "surface_source_cases.hpp"

#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE("I01 actual fitted storage and eager phase stops preserve ownership") {
    const auto assertions_before = assertion_counter().load();
    surface_source_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
