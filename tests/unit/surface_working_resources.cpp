// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface_working.hpp"
#include "surface_working_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
namespace docenhance::tests {
TEST_CASE("Working surface maximum sorter bank is actually charged and refunded before cell work") {
    constexpr std::size_t bank = std::size_t{128} * 1024 * 1024;
    constexpr std::size_t work_and_logs = 4096;
    SurfaceWorkingFixture fixture{4096, 4096, bank + work_and_logs};
    std::ranges::fill(fixture.luminance.view().storage(), 0.5);
    const auto held = fixture.budget.used();
    // Empty mask: one census checkpoint then the first real cell-row checkpoint.
    const CheckpointStop stop{core::Checkpoint::measurement, 1};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = methods::apply(methods::Surface::create({.cell = 512}).value(),
                                       {fixture.luminance.view(), {}, fixture.budget, scheduler});
    REQUIRE(!result);
    REQUIRE(result.error().code == core::ErrorCode::cancelled);
    REQUIRE(CheckpointStop::visits() == 2);
    REQUIRE(fixture.budget.peak() == held + bank + work_and_logs);
    REQUIRE(fixture.budget.used() == held);
    REQUIRE(
        std::ranges::all_of(fixture.luminance.view().storage(), [](double y) { return y == 0.5; }));
}
TEST_CASE("Working surface actual largest one dimensional hierarchy bounds its real phase") {
    constexpr std::size_t work_and_logs = std::size_t{4} * 1024 * 1024;
    constexpr std::size_t hierarchy = std::size_t{6} * 1024 * 1024;
    for (const auto shortfall : {std::size_t{0}, std::size_t{1}}) {
        SurfaceWorkingFixture fixture{524288, 8, work_and_logs + hierarchy - shortfall};
        std::ranges::fill(fixture.protection.view().storage(), std::uint8_t{255});
        for (std::uint32_t y = 0; y < 2; ++y) {
            std::ranges::fill(fixture.protection.view().row(y).first(131072), std::uint8_t{0});
        }
        const auto held = fixture.budget.used();
        // The initial true-residual operator visits 65536/256 blocks. Next visit is inside
        // the real V-cycle, after the unchanged hierarchy owner has acquired its six rows.
        const CheckpointStop stop{core::Checkpoint::solving, 256};
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                        stop.cancellation()};
        const auto result =
            methods::apply(methods::Surface::create({.cell = 8}).value(),
                           {fixture.luminance.view(), fixture.protection.view().as_const(),
                            fixture.budget, scheduler});
        REQUIRE(!result);
        REQUIRE(fixture.budget.used() == held);
        if (shortfall == 0) {
            REQUIRE(result.error().code == core::ErrorCode::cancelled);
            REQUIRE(CheckpointStop::visits() == 257);
            REQUIRE(fixture.budget.peak() == held + work_and_logs + hierarchy);
        } else {
            REQUIRE(result.error().code == core::ErrorCode::resource);
            REQUIRE(CheckpointStop::visits() == 256);
            REQUIRE(fixture.budget.peak() == held + work_and_logs + 32768);
        }
        for (std::uint32_t y = 0; y < 8; ++y) {
            const auto row = fixture.luminance.view().row(y);
            bool unchanged = true;
            for (std::uint32_t x = 0; x < row.size(); ++x) {
                const auto original =
                    0.4 + (static_cast<double>(((17 * x) + (13 * y)) % 256) / 1024);
                unchanged = unchanged && row.subspan(x, 1).front() == original;
            }
            REQUIRE(unchanged);
        }
    }
}
} // namespace docenhance::tests
