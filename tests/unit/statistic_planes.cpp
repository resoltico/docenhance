// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "require.hpp"
#include "statistic_plane_cases.hpp"

#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE("Uint32 statistic planes retain actual charge typed values borrowing moves and refund") {
    const auto assertions_before = assertion_counter().load();
    statistic_plane_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
