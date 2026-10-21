// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/denoising.hpp"
#include "require.hpp"
#include "tvl1_detail.hpp"
#include "tvl1_stop_cases.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace docenhance::tests {
namespace {
void tvl1_stop_capture_case() {
    static_assert(std::is_nothrow_destructible_v<methods::Tvl1StopCapture>);
    static_assert(
        std::is_nothrow_constructible_v<methods::Tvl1StopCapture, const methods::Tvl1SolveState&,
                                        std::optional<methods::Tvl1Stop>&>);
    static_assert(std::is_nothrow_assignable_v<std::optional<methods::Tvl1Stop>&,
                                               const std::optional<methods::Tvl1Stop>&>);
    std::optional<methods::Tvl1Stop> observed;
    try {
        methods::Tvl1SolveState progress;
        const methods::Tvl1StopCapture capture{progress, observed};
        progress.stop = methods::Tvl1Stop::tolerance_met;
        throw std::runtime_error("controlled capture unwind");
    } catch (const std::runtime_error&) {
        require(observed == methods::Tvl1Stop::tolerance_met,
                "mechanism-only controlled unwind captures before local progress dies");
    }
}
} // namespace
TEST_CASE("Working TV-L1 retains actual stops through objective and mapping failure") {
    const auto assertions_before = assertion_counter().load();
    tvl1_stop_cases();
    tvl1_stop_capture_case();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
