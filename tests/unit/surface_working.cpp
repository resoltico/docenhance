// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "require.hpp"
#include "surface_working_boundaries.hpp"
#include "surface_working_cases.hpp"
#include "surface_working_controls.hpp"

#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE("Working surface independent dense field and complete facts agree across workers") {
    const auto assertions_before = assertion_counter().load();
    surface_worker_cases();
    surface_absent_mask_case();
    surface_full_fallback_case();
    surface_dark_reference_case();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working surface no-op automatic and gain boundaries retain attained observations") {
    const auto assertions_before = assertion_counter().load();
    surface_noop_cases();
    surface_automatic_cases();
    surface_gain_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working surface actual aligned phase acquisitions and deterministic stops refund") {
    const auto assertions_before = assertion_counter().load();
    surface_resource_cases();
    surface_cancel_cases();
    surface_grid_noop_case();
    surface_hierarchy_working_case();
    surface_prefix_case(false);
    surface_prefix_case(true);
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
