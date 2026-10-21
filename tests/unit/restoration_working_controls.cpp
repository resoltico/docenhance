// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/restoration_working.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "restoration_working_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace docenhance::tests {
namespace {
auto restore(RestorationWorkingFixture& fixture, const exec::Scheduler& scheduler) {
    const auto method = methods::Wiener::create({.k = 0.1}).value();
    const auto psf = restoration_gaussian();
    return methods::apply(method, psf,
                          {fixture.luminance.view(), fixture.protection.view().as_const(),
                           fixture.budget, scheduler});
}
std::size_t phase_visits(core::Checkpoint phase) {
    RestorationWorkingFixture fixture{7, 3};
    const CheckpointStop trace{phase, std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    trace.cancellation()};
    const auto held = fixture.budget.used();
    const auto result = restore(fixture, scheduler);
    require(result && result->counts.changed > 0 && fixture.budget.used() == held,
            "healthy public restoration calibrates real reached phases and refunds owners");
    return CheckpointStop::visits();
}
void phase_stop(core::Checkpoint phase, std::size_t allowed) {
    RestorationWorkingFixture fixture{7, 3};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> mask_before(mask.begin(), mask.end());
    const auto held = fixture.budget.used();
    const CheckpointStop stop{phase, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = restore(fixture, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1 && fixture.budget.used() == held &&
                std::ranges::equal(mask, mask_before),
            "actual public phase stop returns no completed Facts and refunds all joined owners");
    const auto after = gamma_bits(fixture.luminance.view().storage());
    const auto pitch = fixture.luminance.view().row_pitch();
    for (std::size_t i = 0; i < after.size(); ++i) {
        if (i % pitch >= fixture.luminance.width()) {
            require(after.at(i) == before.at(i),
                    "all actual phase stops preserve every Y padding bit");
        }
    }
    if (phase == core::Checkpoint::allocation || phase == core::Checkpoint::initialization ||
        phase == core::Checkpoint::measurement || phase == core::Checkpoint::solving) {
        require(after == before,
                "every actual premap phase stop preserves the complete entering Y backing");
    }
}
void protect_prefix(RestorationWorkingFixture& fixture) {
    for (std::uint32_t y = 0; y < fixture.protection.height(); ++y) {
        std::ranges::fill(fixture.protection.view().row(y).first(32), 1);
    }
}
struct MappingTrace {
    std::size_t entry;
    std::vector<std::uint64_t> before;
    std::vector<std::uint64_t> completed;
};
MappingTrace mapping_trace() {
    RestorationWorkingFixture fixture{773, 3};
    protect_prefix(fixture);
    auto before = gamma_bits(fixture.luminance.view().storage());
    const CheckpointStop trace{core::Checkpoint::processing,
                               std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    trace.cancellation()};
    const auto result = restore(fixture, scheduler);
    require(result && result->counts.changed > 0 && CheckpointStop::visits() > 13,
            "healthy actual restoration reaches mapping after real Fourier work");
    auto completed = gamma_bits(fixture.luminance.view().storage());
    const auto pitch = fixture.luminance.view().row_pitch();
    for (std::size_t i = 0; i < completed.size(); ++i) {
        if (i % pitch < 32 || i % pitch >= 773) {
            require(completed.at(i) == before.at(i),
                    "healthy public restoration independently preserves protected Y and padding");
        }
    }
    const auto last = (2 * fixture.luminance.view().row_pitch()) + 772;
    require(before.at(32) != completed.at(32) && before.at(last) != completed.at(last),
            "actual eligible prefix and final suffix both witness completed stores");
    // Three rows each reach x=0/256/512/768, then the single band's final facts fold.
    return {
        .entry = CheckpointStop::visits() - 13,
        .before = std::move(before),
        .completed = std::move(completed),
    };
}
void mapping_stop(const MappingTrace& trace, std::size_t blocks) {
    RestorationWorkingFixture fixture{773, 3};
    protect_prefix(fixture);
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> mask_before(mask.begin(), mask.end());
    const auto held = fixture.budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, trace.entry + blocks};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = restore(fixture, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == trace.entry + blocks + 1 &&
                fixture.budget.used() == held && std::ranges::equal(mask, mask_before),
            "interior actual mapping cancellation returns no Facts and refunds after all joins");
    const auto after = gamma_bits(fixture.luminance.view().storage());
    const auto pitch = fixture.luminance.view().row_pitch();
    for (std::size_t i = 0; i < after.size(); ++i) {
        const auto y = i / pitch;
        const auto x = i % pitch;
        const auto mapped = x >= 32 && x < 773 && (y * 4) + (x / 256) < blocks;
        require(after.at(i) == (mapped ? trace.completed.at(i) : trace.before.at(i)),
                "each mapped block matches actual completion; untouched pixels and padding "
                "retain entering bits including protected samples");
    }
}
std::size_t scratch_peak() {
    RestorationWorkingFixture fixture{773, 3};
    const auto held = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = restore(fixture, scheduler);
    require(result && fixture.budget.used() == held && fixture.budget.peak() > held,
            "healthy one-worker restoration establishes minimum charged scratch high water");
    std::cout << R"({"restoration_workers":1,"resident":)" << held
              << ",\"charged_scratch_peak\":" << fixture.budget.peak() - held << "}\n";
    return fixture.budget.peak() - held;
}
void scratch_limit(std::size_t capacity, bool succeeds) {
    RestorationWorkingFixture fixture{773, 3, capacity};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto held = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = restore(fixture, scheduler);
    require(fixture.budget.used() == held,
            "exact and refused charged capacities both refund all joined scratch owners");
    if (succeeds) {
        require(result && fixture.budget.peak() == held + capacity,
                "exact measured charged high water suffices for the same public operation");
    } else {
        require(!result && result.error().code == core::ErrorCode::resource &&
                    gamma_bits(fixture.luminance.view().storage()) == before,
                "one byte less refuses real acquisition before any mapping store");
    }
}
void scratch_worker_fit(std::size_t capacity) {
    RestorationWorkingFixture serial{773, 3, capacity};
    RestorationWorkingFixture parallel{773, 3, capacity};
    const auto serial_held = serial.budget.used();
    const auto parallel_held = parallel.budget.used();
    const exec::Scheduler one{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const exec::Scheduler seven{exec::Concurrency::resolve(7, 7, 0, 0).value()};
    const auto expected = restore(serial, one);
    const auto actual = restore(parallel, seven);
    require(expected && actual && *expected == *actual &&
                gamma_bits(serial.luminance.view().storage()) ==
                    gamma_bits(parallel.luminance.view().storage()) &&
                serial.budget.used() == serial_held && parallel.budget.used() == parallel_held &&
                serial.budget.peak() <= serial_held + capacity &&
                parallel.budget.peak() <= parallel_held + capacity,
            "a requested seven-worker bound fits minimum scratch with identical facts and bits");
}
} // namespace
TEST_CASE("Working restoration cancels at real first interior and final phase boundaries") {
    const auto assertions_before = assertion_counter().load();
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::measurement,
             core::Checkpoint::solving,
             core::Checkpoint::processing,
             core::Checkpoint::scheduling,
         }) {
        const auto visits = phase_visits(phase);
        require(visits > 3, "each actual public restoration phase has an interior boundary");
        for (const auto allowed : {std::size_t{0}, std::size_t{1}, std::size_t{3}, visits - 1}) {
            phase_stop(phase, allowed);
        }
    }
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working restoration partial mapping preserves exact stores masks and padding") {
    const auto assertions_before = assertion_counter().load();
    const auto trace = mapping_trace();
    for (const auto blocks : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{12}}) {
        mapping_stop(trace, blocks);
    }
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working restoration charged scratch accepts its minimum and refuses one byte less") {
    const auto assertions_before = assertion_counter().load();
    const auto peak = scratch_peak();
    scratch_limit(peak, true);
    scratch_limit(peak - 1, false);
    scratch_worker_fit(peak);
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
