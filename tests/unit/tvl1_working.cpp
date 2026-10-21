// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/denoising.hpp"
#include "require.hpp"
#include "tvl1_detail.hpp"
#include "tvl1_parallel_cases.hpp"
#include "tvl1_working_cases.hpp"
#include "tvl1_working_controls.hpp"

#include <catch2/catch_test_macros.hpp>

namespace docenhance::tests {
TEST_CASE("Working TV-L1 matches independent solve and exact worker output facts") {
    const auto assertions_before = assertion_counter().load();
    tvl1_active_cases();
    tvl1_precision_case();
    tvl1_parallel_state_case();
    tvl1_objective_admission_case();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working TV-L1 constants high-fidelity dual motion protection and rounded stores") {
    const auto assertions_before = assertion_counter().load();
    tvl1_constant_cases();
    tvl1_high_lambda_case();
    tvl1_equal_decoded_write_case();
    tvl1_context_and_rounding_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working TV-L1 admission five-plane charges and exact phase cancellation") {
    const auto assertions_before = assertion_counter().load();
    tvl1_noop_and_admission_cases();
    tvl1_factory_cases();
    tvl1_resource_cases();
    tvl1_cancellation_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("TV-L1 actual checkpoint policy resets and retains noncheckpoint streaks") {
    const auto method = methods::Tvl1::create({.iterations = 50, .tolerance = 1e-5}).value();
    methods::Tvl1SolveState progress;
    progress.iterations = 20;
    CHECK(!methods::tvl1_checkpoint(progress, method));
    CHECK(progress.passing_checkpoints == 1);
    progress.iterations = 29;
    progress.dual_update = 1;
    CHECK(!methods::tvl1_checkpoint(progress, method));
    CHECK(progress.passing_checkpoints == 1);
    progress.iterations = 30;
    CHECK(!methods::tvl1_checkpoint(progress, method));
    CHECK(progress.passing_checkpoints == 0);
    progress.iterations = 40;
    progress.dual_update = 0;
    CHECK(!methods::tvl1_checkpoint(progress, method));
    CHECK(progress.passing_checkpoints == 1);
    progress.iterations = 50;
    CHECK(methods::tvl1_checkpoint(progress, method));
    CHECK(progress.passing_checkpoints == 2);
    CHECK(progress.stop == methods::Tvl1Stop::tolerance_met);
}
} // namespace docenhance::tests
