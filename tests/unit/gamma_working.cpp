// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "gamma_working_cases.hpp"
#include "require.hpp"

#include <catch2/catch_test_macros.hpp>

namespace docenhance::tests {
TEST_CASE("Working Gamma reference and partitions preserve actual samples and facts") {
    const auto assertions_before = assertion_counter().load();
    gamma_active_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working Gamma identities and perceptual write events remain distinct") {
    const auto assertions_before = assertion_counter().load();
    gamma_identity_cases();
    gamma_write_event_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working Gamma admission and deterministic cancellation keep execution truthful") {
    const auto assertions_before = assertion_counter().load();
    gamma_cancellation_cases();
    gamma_admission_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
