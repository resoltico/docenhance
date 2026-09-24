// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/cli/run.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/box_mean.hpp"
#include "docenhance/methods/fixed_threshold.hpp"
#include "docenhance/methods/sauvola.hpp"
#include "processor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <limits>
#include <sstream>
#include <stop_token>
#include <string>

namespace docenhance::tests {
namespace {
constexpr std::size_t allocation_limit = std::size_t{4} * 1024 * 1024;
core::Cancellation stopped_token() {
    const std::stop_source source;
    static_cast<void>(source.request_stop());
    return core::Cancellation{source.get_token()};
}
contract::Invocation invocation() {
    return {
        .command = contract::Command::process,
        .subject = "unopened.png",
        .output_directory = "uncreated",
        .binarize = "fixed",
    };
}
exec::Concurrency workers(unsigned count) {
    return exec::Concurrency::resolve(count, count, 0, 0).value();
}
} // namespace
TEST_CASE("Cancellation owns its stop state and prevents application effects", "[cancellation]") {
    const auto cancellation = stopped_token();
    CHECK(cancellation.requested(core::Checkpoint::admission));
    RejectingProcessor processor;
    auto request = invocation();
    const auto result = app::dispatch(request, processor, cancellation);
    CHECK(processor.calls == 0);
    CHECK(result.exit_code() == core::ExitCode::cancelled);
    const auto& error = std::get<app::Failure>(result.payload).error;
    CHECK(error.identifier() == "E_CANCELLED");
    CHECK(error.publication == core::Publication::not_started);
    request.binarize = "invalid";
    CHECK(app::dispatch(request, processor, cancellation).exit_code() ==
          core::ExitCode::invocation);
    request.help = true;
    CHECK(app::dispatch(request, processor, cancellation).exit_code() == core::ExitCode::success);
    CHECK(processor.calls == 0);
}
TEST_CASE("Cancelled CLI processing produces one normal JSON response", "[cancellation]") {
    constexpr auto args = std::to_array<const char*>({
        "docenhance",
        "process",
        "absent.png",
        "--out-dir",
        "absent",
        "--binarize",
        "fixed",
        "--json",
    });
    RejectingProcessor processor;
    std::ostringstream out;
    std::ostringstream err;
    CHECK(cli::run(args, processor, out, err, stopped_token()) == 130);
    CHECK(processor.calls == 0);
    CHECK(err.str().empty());
    CHECK(out.str().contains("E_CANCELLED"));
    CHECK(out.str().contains("not_started"));
    out.str("");
    out.setstate(std::ios::badbit);
    CHECK(cli::run(args, processor, out, err, stopped_token()) == 5);
    CHECK(processor.calls == 0);
    CHECK(err.str().empty());
}
TEST_CASE("The scheduler does not call work after a cancellation checkpoint", "[cancellation]") {
    for (const unsigned count : {1U, 2U, 8U}) {
        const exec::Scheduler scheduler{workers(count), stopped_token()};
        std::atomic<unsigned> called{0};
        const auto task = [&](std::size_t) {
            ++called;
            return core::Result<void>{};
        };
        CHECK(scheduler.for_each(0, exec::WorkRef{task}));
        const auto result =
            scheduler.for_each(std::numeric_limits<std::size_t>::max(), exec::WorkRef{task});
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(called.load() == 0);
    }
    const std::stop_source source;
    const exec::Scheduler scheduler{workers(1), core::Cancellation{source.get_token()}};
    unsigned called = 0;
    const auto task = [&](std::size_t) {
        ++called;
        static_cast<void>(source.request_stop());
        return core::Result<void>{};
    };
    CHECK(scheduler.for_each(2, exec::WorkRef{task}).error().code == core::ErrorCode::cancelled);
    CHECK(called == 1);
}
TEST_CASE("Real worker failure outranks simultaneous cancellation and all workers join",
          "[cancellation]") {
    const std::stop_source source;
    const exec::Scheduler scheduler{workers(2), core::Cancellation{source.get_token()}};
    std::barrier started{2};
    std::atomic<unsigned> finished{0};
    const auto task = [&](std::size_t index) -> core::Result<void> {
        started.arrive_and_wait();
        ++finished;
        if (index == 0) {
            static_cast<void>(source.request_stop());
            return core::cancelled();
        }
        return core::failure(core::ErrorCode::input, "A real task error");
    };
    const auto result = scheduler.for_each(2, exec::WorkRef{task});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::input);
    CHECK(finished.load() == 2);
}
TEST_CASE("Fixed threshold cancellation reaches wide-row interiors", "[cancellation]") {
    core::Budget budget{allocation_limit};
    auto input = image::Plane<std::uint8_t>::allocate(budget, 4096, 1).value();
    auto output = image::Plane<std::uint8_t>::allocate(budget, 4096, 1).value();
    std::ranges::fill(input.view().storage(), UINT8_MAX);
    constexpr std::uint8_t untouched = 17;
    std::ranges::fill(output.view().storage(), untouched);
    const CheckpointStop stop{core::Checkpoint::processing, 1};
    const auto result =
        methods::fixed_threshold(input.view().as_const(), output.view(), 0.5, stop.cancellation());
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(output.view().row(0).front() == UINT8_MAX);
    CHECK(output.view().row(0).back() == untouched);
    CHECK(std::ranges::all_of(input.view().storage(), [](auto v) { return v == UINT8_MAX; }));
}
TEST_CASE("Sauvola cancellation covers scratch admission and strip interiors", "[cancellation]") {
    core::Budget budget{allocation_limit};
    auto input = image::Plane<std::uint8_t>::allocate(budget, 8192, 32).value();
    auto output = image::Plane<std::uint8_t>::allocate(budget, 8192, 32).value();
    std::ranges::fill(input.view().storage(), UINT8_MAX);
    const auto method = methods::Sauvola::create().value();
    const auto held = budget.used();
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::processing,
         }) {
        constexpr std::uint8_t untouched = 17;
        std::ranges::fill(output.view().storage(), untouched);
        const CheckpointStop stop{phase, phase == core::Checkpoint::allocation ? 0U : 2U};
        const exec::Scheduler scheduler{workers(2), stop.cancellation()};
        const auto result = methods::sauvola(input.view().as_const(), output.view(), method,
                                             {.scheduler = scheduler, .budget = budget});
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(budget.used() == held);
        if (phase != core::Checkpoint::processing) {
            CHECK(std::ranges::all_of(output.view().storage(),
                                      [](auto v) { return v == untouched; }));
        }
    }
}
TEST_CASE("Box mean cancellation interrupts reflected initialization and both passes",
          "[cancellation]") {
    core::Budget budget{allocation_limit};
    auto input = image::Plane<float>::allocate(budget, 4096, 2).value();
    auto output = image::Plane<float>::allocate(budget, 4096, 2).value();
    std::ranges::fill(input.view().storage(), 1.0F);
    const auto held = budget.used();
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::processing,
         }) {
        const CheckpointStop stop{phase, 0};
        const exec::Scheduler scheduler{workers(1), stop.cancellation()};
        const auto result =
            methods::box_mean(input.view().as_const(), output.view(), 100000, scheduler, budget);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(budget.used() == held);
    }
}
TEST_CASE("Box mean cancellation reaches the second pass", "[cancellation]") {
    core::Budget budget{allocation_limit};
    auto input = image::Plane<float>::allocate(budget, 32, 3).value();
    auto output = image::Plane<float>::allocate(budget, 32, 3).value();
    std::ranges::fill(input.view().storage(), 1.0F);
    constexpr float untouched = 17.0F;
    std::ranges::fill(output.view().storage(), untouched);
    const auto held = budget.used();
    // Three horizontal rows and one vertical output row complete before the next checkpoint.
    const CheckpointStop stop{core::Checkpoint::processing, 4};
    const exec::Scheduler scheduler{workers(1), stop.cancellation()};
    const auto result =
        methods::box_mean(input.view().as_const(), output.view(), 3, scheduler, budget);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(budget.used() == held);
    CHECK(output.view().row(0).front() == 1.0F);
    CHECK(output.view().row(1).front() == untouched);
}
} // namespace docenhance::tests
