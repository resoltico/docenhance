// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "morphology_parallel_cases.hpp"
#include "morphology_working_cases.hpp"
#include "morphology_working_controls.hpp"
#include "require.hpp"

#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE("Working morphology matches independent fields gains and complete facts across workers") {
    const auto assertions_before = assertion_counter().load();
    morphology_absent_mask_case();
    morphology_worker_case();
    morphology_constant_and_excursion_cases();
    morphology_fallback_case();
    morphology_gain_boundary_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working morphology maximal support and explicit intervals retain exact owners") {
    const auto assertions_before = assertion_counter().load();
    morphology_maximum_worker_case(false);
    morphology_maximum_worker_case(true);
    morphology_interval_cases();
    morphology_negative_zero_case();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working morphology real phase acquisitions refusals and stops refund ownership") {
    const auto assertions_before = assertion_counter().load();
    morphology_resource_cases();
    morphology_sample_cap_case();
    morphology_noop_and_cold_cases();
    morphology_phase_cancel_cases();
    morphology_map_healthy_case(false);
    morphology_map_healthy_case(true);
    morphology_map_stop_case(4154, false);
    morphology_map_stop_case(4155, false);
    morphology_map_stop_case(4155, true);
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
