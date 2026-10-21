// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "levels_working_cases.hpp"
#include "require.hpp"

#include <catch2/catch_test_macros.hpp>

namespace docenhance::tests {
TEST_CASE("Working Levels fit and mapping preserve worker-independent reference results") {
    const auto assertions_before = assertion_counter().load();
    levels_active_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working Levels narrow fits no-ops and write events retain their actual meaning") {
    const auto assertions_before = assertion_counter().load();
    levels_flat_cases();
    levels_noop_and_write_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working Levels aligned resources and deterministic phase cancellation remain truthful") {
    const auto assertions_before = assertion_counter().load();
    levels_resource_cases();
    levels_cancellation_cases();
    levels_admission_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
