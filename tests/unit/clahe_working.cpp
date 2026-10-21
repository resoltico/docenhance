// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "clahe_working_cases.hpp"
#include "clahe_working_controls.hpp"
#include "contrast_detail.hpp"
#include "require.hpp"

#include <catch2/catch_test_macros.hpp>

namespace docenhance::tests {
TEST_CASE("Working CLAHE active reference results and exact worker determinism") {
    CHECK(methods::tile_index(16, 33, 2) == 1);
    clahe_active_cases();
}
TEST_CASE("Working CLAHE identity cutoff and dense white write events") {
    const auto assertions_before = assertion_counter().load();
    clahe_identity_cases();
    for (const auto count : {15U, 16U}) {
        clahe_cutoff_case(count, 1.0 / 8192);
        clahe_cutoff_case(count, 1.0 / 4096);
    }
    clahe_white_write_case();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working CLAHE admission charged resources and deterministic cancellation") {
    const auto assertions_before = assertion_counter().load();
    clahe_noop_cases();
    clahe_admission_cases();
    clahe_resource_cases();
    clahe_cancellation_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
