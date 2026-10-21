// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "illumination_receiver_cases.hpp"
#include "morphology_kernel_cases.hpp"
#include "require.hpp"

#include <catch2/catch_test_macros.hpp>

namespace docenhance::tests {
TEST_CASE("I02 actual eager phase stops preserve input and refund owned storage") {
    const auto assertions_before = assertion_counter().load();
    morphology_phase_boundary_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("I02 and I01 eager receivers preserve identity correction and structural stop order") {
    const auto assertions_before = assertion_counter().load();
    illumination_receiver_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("I02 maximal support preserves admitted state and real interior failure") {
    const auto assertions_before = assertion_counter().load();
    morphology_kernel_baseline_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
