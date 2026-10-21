// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "nlm_numerical_cases.hpp"
#include "nlm_phase_cases.hpp"
#include "nlm_working_cases.hpp"
#include "nlm_working_controls.hpp"
#include "require.hpp"

#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE(
    "Working NLM independent direct reference and full worker determinism cross eight bands") {
    const auto assertions_before = assertion_counter().load();
    nlm_worker_cases();
    nlm_absent_mask_case();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working NLM exact typed resources no-op priority and postjoin cancellation are real") {
    const auto assertions_before = assertion_counter().load();
    nlm_exact_resource_cases();
    nlm_noop_cases();
    nlm_reducer_stop_case();
    nlm_phase_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working NLM retains constant-code bits exact borders fractional means and decoded-equal "
          "writes") {
    const auto assertions_before = assertion_counter().load();
    nlm_constant_code_cases();
    nlm_border_and_mean_cases();
    nlm_decoded_equal_case();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
