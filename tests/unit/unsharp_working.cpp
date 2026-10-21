// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "gaussian_band_cases.hpp"
#include "require.hpp"
#include "unsharp_working_cases.hpp"
#include "unsharp_working_controls.hpp"

#include <catch2/catch_test_macros.hpp>

namespace docenhance::tests {
TEST_CASE("Working unsharp agrees with direct2D reference and exact worker determinism") {
    const auto assertions_before = assertion_counter().load();
    unsharp_active_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE(
    "Working unsharp exact identities endpoint writes and protected context remain truthful") {
    const auto assertions_before = assertion_counter().load();
    unsharp_constant_and_rounding_cases();
    unsharp_endpoint_case();
    unsharp_context_case();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE(
    "Working unsharp admission aligned fields and deterministic phase stops preserve owners") {
    const auto assertions_before = assertion_counter().load();
    unsharp_noop_cases();
    unsharp_admission_cases();
    unsharp_resource_cases();
    unsharp_cancellation_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Shared Gaussian explicit bands preserve whole-pass results and actual admission") {
    const auto assertions_before = assertion_counter().load();
    gaussian_band_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
