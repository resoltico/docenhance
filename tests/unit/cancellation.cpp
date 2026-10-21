// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/cli/run.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/box_mean.hpp"
#include "processor.hpp"
#include "require.hpp"
#include "stub_verifier.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <iostream>
#include <limits>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace docenhance::tests {
namespace {
UnusedVerifier verifier;
} // namespace
namespace {
constexpr std::size_t allocation_limit = std::size_t{4} * 1024 * 1024;
core::Cancellation stopped_token() {
    std::stop_source source;
    request_stop(source);
    return core::Cancellation{source.get_token()};
}
app::Invocation invocation() {
    app::Invocation value;
    value.command = app::Command::process;
    value.operands = {"unopened.png"};
    value.option(app::OptionId::out_dir) = "uncreated";
    value.option(app::OptionId::output_mode) = "bw";
    value.option(app::OptionId::binarize) = "fixed";
    return value;
}
exec::Concurrency workers(unsigned count) {
    const auto result = exec::Concurrency::resolve(count, count, 0, 0);
    REQUIRE(result);
    return *result;
}
void check_white_sauvola_backing(image::PlaneView<const std::uint8_t> output) {
    const auto storage = output.storage();
    for (std::size_t i = 0; i < storage.size(); ++i) {
        require(storage[i] == (i % output.row_pitch() < output.width() ? 1 : 17),
                "healthy constant pixels are white and all destination padding stays sentinel");
    }
}
std::size_t check_sauvola_prefix(image::PlaneView<const std::uint8_t> output,
                                 image::PlaneView<const std::uint8_t> healthy,
                                 const std::vector<std::uint8_t>& before) {
    const auto storage = output.storage();
    std::size_t changed = 0;
    for (std::size_t i = 0; i < storage.size(); ++i) {
        changed += static_cast<std::size_t>(storage[i] != before.at(i));
        if (i < 1024) {
            require(storage[i] == healthy.storage()[i], "actual prefix matches healthy output");
            require(storage[i] != before.at(i), "actual prefix changed from entering sentinel");
        } else {
            require(storage[i] == before.at(i), "suffix later row and padding remain untouched");
        }
    }
    return changed;
}
std::size_t healthy_sauvola_prefix(const methods::Sauvola& method,
                                   image::PlaneView<const std::uint16_t> source,
                                   image::PlaneView<std::uint8_t> healthy, core::Budget& budget,
                                   const std::vector<std::uint16_t>& source_before) {
    const auto held = budget.used();
    const CheckpointStop trace{core::Checkpoint::processing,
                               std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{workers(1), trace.cancellation()};
    require(methods::binarize(method, {source, healthy, budget, scheduler}).has_value(),
            "actual one-worker healthy Sauvola counterpart completes");
    const auto healthy_visits = CheckpointStop::visits();
    require(healthy_visits == 7,
            "healthy actual row/interior plus final-count-fold checkpoints calibrate seven visits");
    require(budget.used() == held, "healthy workspace refunds to resident charge");
    require(std::ranges::equal(source.storage(), source_before),
            "healthy full source backing stays exact");
    check_white_sauvola_backing(healthy.as_const());
    return healthy_visits;
}
void sauvola_interior_prefix() {
    core::Budget budget{allocation_limit};
    auto input = image::Plane<std::uint16_t>::allocate(budget, 1031, 2);
    auto output = image::Plane<std::uint8_t>::allocate(budget, 1031, 2);
    auto healthy = image::Plane<std::uint8_t>::allocate(budget, 1031, 2);
    REQUIRE(input);
    REQUIRE(output);
    REQUIRE(healthy);
    std::ranges::fill(input->view().storage(), UINT16_MAX);
    std::ranges::fill(output->view().storage(), 17);
    std::ranges::fill(healthy->view().storage(), 17);
    const auto source = input->view().storage();
    const std::vector<std::uint16_t> source_before(source.begin(), source.end());
    const auto entering = output->view().storage();
    const std::vector<std::uint8_t> before(entering.begin(), entering.end());
    const auto method = methods::Sauvola::create();
    REQUIRE(method);
    const auto held = budget.used();
    const auto healthy_visits = healthy_sauvola_prefix(*method, input->view().as_const(),
                                                       healthy->view(), budget, source_before);
    const CheckpointStop stop{core::Checkpoint::processing, 2};
    const exec::Scheduler stopped{workers(1), stop.cancellation()};
    const auto result =
        methods::binarize(*method, {input->view().as_const(), output->view(), budget, stopped});
    require(!result && result.error().code == core::ErrorCode::cancelled,
            "actual interior stop returns typed cancellation");
    require(CheckpointStop::visits() == 3,
            "actual interior stop attains its third processing poll");
    require(budget.used() == held, "cancelled workspace refunds to resident charge");
    require(std::ranges::equal(source, source_before), "cancelled full source backing stays exact");
    const auto changed =
        check_sauvola_prefix(output->view().as_const(), healthy->view().as_const(), before);
    require(changed == 1024, "exactly the first 1024 logical destinations changed");
    std::cout << "Sauvola single-worker prefix: healthy_visits=" << healthy_visits
              << " stop_visits=" << CheckpointStop::visits() << " changed=" << changed
              << " resident=" << held << " refunded=" << (budget.used() == held) << "\n";
}
// A meeting point every task reaches before any of them returns, so simultaneity is arranged
// rather than hoped for. It is written out of one atomic because libc++ settles a standard
// barrier inside an uninstrumented library function: ThreadSanitizer cannot see the ordering that
// function establishes, so it reports the barrier's own bookkeeping as a race and the test says
// nothing about the scheduler underneath it.
class Rendezvous {
  public:
    explicit Rendezvous(unsigned expected) noexcept : expected_(expected) {}
    void arrive_and_wait() noexcept {
        arrived_.fetch_add(1, std::memory_order_acq_rel);
        while (arrived_.load(std::memory_order_acquire) < expected_) {
            std::this_thread::yield();
        }
    }

  private:
    unsigned expected_;
    std::atomic<unsigned> arrived_{0};
};
} // namespace
TEST_CASE("Cancellation owns its stop state and prevents application effects", "[cancellation]") {
    const auto cancellation = stopped_token();
    CHECK(cancellation.requested(core::Checkpoint::admission));
    RejectingProcessor processor;
    auto request = invocation();
    const auto result = app::dispatch(request, processor, verifier, cancellation);
    CHECK(processor.calls == 0);
    CHECK(result.exit_code() == core::ExitCode::cancelled);
    const auto& failed = std::get<app::Failure>(result.payload);
    CHECK(std::string(failed.error.identifier()) == "E_CANCELLED");
    CHECK(failed.publication == core::Publication::not_started);
    request.option(app::OptionId::binarize) = "invalid";
    CHECK(app::dispatch(request, processor, verifier, cancellation).exit_code() ==
          core::ExitCode::invocation);
    request.option(app::OptionId::help).emplace();
    CHECK(app::dispatch(request, processor, verifier, cancellation).exit_code() ==
          core::ExitCode::success);
    CHECK(processor.calls == 0);
}
TEST_CASE("Cancelled CLI processing produces one normal JSON response", "[cancellation]") {
    constexpr auto args = std::to_array<const char*>({
        "docenhance",
        "process",
        "absent.png",
        "--out-dir",
        "absent",
        "--output-mode",
        "bw",
        "--binarize",
        "fixed",
        "--json",
    });
    RejectingProcessor processor;
    std::ostringstream out;
    std::ostringstream err;
    CHECK(cli::run(args, {.processor = processor, .verifier = verifier}, out, err,
                   stopped_token()) == 130);
    CHECK(processor.calls == 0);
    CHECK(err.str().empty());
    CHECK(out.str().contains("E_CANCELLED"));
    CHECK(out.str().contains("not_started"));
    out.str("");
    out.setstate(std::ios::badbit);
    CHECK(cli::run(args, {.processor = processor, .verifier = verifier}, out, err,
                   stopped_token()) == 5);
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
    std::stop_source source;
    const exec::Scheduler scheduler{workers(1), core::Cancellation{source.get_token()}};
    unsigned called = 0;
    const auto task = [&](std::size_t) {
        ++called;
        request_stop(source);
        return core::Result<void>{};
    };
    CHECK(scheduler.for_each(2, exec::WorkRef{task}).error().code == core::ErrorCode::cancelled);
    CHECK(called == 1);
}
TEST_CASE("Real worker failure outranks simultaneous cancellation and all workers join",
          "[cancellation]") {
    std::stop_source source;
    const exec::Scheduler scheduler{workers(2), core::Cancellation{source.get_token()}};
    Rendezvous started{2};
    std::atomic<unsigned> finished{0};
    const auto task = [&](std::size_t index) -> core::Result<void> {
        started.arrive_and_wait();
        ++finished;
        if (index == 0) {
            request_stop(source);
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
    auto input = image::Plane<std::uint16_t>::allocate(budget, 4096, 1);
    auto output = image::Plane<std::uint8_t>::allocate(budget, 4096, 1);
    REQUIRE(input);
    REQUIRE(output);
    std::ranges::fill(input->view().storage(), UINT16_MAX);
    constexpr std::uint8_t untouched = 17;
    std::ranges::fill(output->view().storage(), untouched);
    const auto held = budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, 1};
    const auto method = methods::FixedThreshold::create(0.5);
    REQUIRE(method);
    const exec::Scheduler scheduler{workers(1), stop.cancellation()};
    const auto result =
        methods::binarize(*method, {input->view().as_const(), output->view(), budget, scheduler});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(output->view().row(0).front() == 1);
    CHECK(output->view().row(0).back() == untouched);
    CHECK(budget.used() == held);
    CHECK(CheckpointStop::visits() == 2);
    for (std::size_t x = 0; x < 4096; ++x) {
        CHECK(output->view().row(0).subspan(x, 1).front() == (x < 1024 ? 1 : untouched));
    }
    CHECK(std::ranges::all_of(input->view().storage(), [](auto v) { return v == UINT16_MAX; }));
}
TEST_CASE("Sauvola cancellation covers scratch admission and strip interiors", "[cancellation]") {
    core::Budget budget{allocation_limit};
    auto input = image::Plane<std::uint16_t>::allocate(budget, 8192, 32);
    auto output = image::Plane<std::uint8_t>::allocate(budget, 8192, 32);
    REQUIRE(input);
    REQUIRE(output);
    std::ranges::fill(input->view().storage(), UINT16_MAX);
    const auto method = methods::Sauvola::create();
    REQUIRE(method);
    const auto held = budget.used();
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::processing,
         }) {
        constexpr std::uint8_t untouched = 17;
        std::ranges::fill(output->view().storage(), untouched);
        const CheckpointStop stop{phase, phase == core::Checkpoint::allocation ? 0U : 2U};
        const exec::Scheduler scheduler{workers(2), stop.cancellation()};
        const auto result = methods::binarize(
            *method, {input->view().as_const(), output->view(), budget, scheduler});
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(budget.used() == held);
        if (phase != core::Checkpoint::processing) {
            CHECK(std::ranges::all_of(output->view().storage(),
                                      [](auto v) { return v == untouched; }));
        }
    }
    const auto assertions_before = assertion_counter().load();
    sauvola_interior_prefix();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Box mean cancellation interrupts reflected initialization and both passes",
          "[cancellation]") {
    core::Budget budget{allocation_limit};
    auto input = image::Plane<float>::allocate(budget, 4096, 2);
    auto output = image::Plane<float>::allocate(budget, 4096, 2);
    REQUIRE(input);
    REQUIRE(output);
    std::ranges::fill(input->view().storage(), 1.0F);
    const auto held = budget.used();
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::processing,
         }) {
        const CheckpointStop stop{phase, 0};
        const exec::Scheduler scheduler{workers(1), stop.cancellation()};
        const auto result =
            methods::box_mean(input->view().as_const(), output->view(), 100000, scheduler, budget);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(budget.used() == held);
    }
}
TEST_CASE("Box mean cancellation reaches the second pass", "[cancellation]") {
    core::Budget budget{allocation_limit};
    auto input = image::Plane<float>::allocate(budget, 32, 3);
    auto output = image::Plane<float>::allocate(budget, 32, 3);
    REQUIRE(input);
    REQUIRE(output);
    std::ranges::fill(input->view().storage(), 1.0F);
    constexpr float untouched = 17.0F;
    std::ranges::fill(output->view().storage(), untouched);
    const auto held = budget.used();
    // Three horizontal rows and one vertical output row complete before the next checkpoint.
    const CheckpointStop stop{core::Checkpoint::processing, 4};
    const exec::Scheduler scheduler{workers(1), stop.cancellation()};
    const auto result =
        methods::box_mean(input->view().as_const(), output->view(), 3, scheduler, budget);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(budget.used() == held);
    CHECK(output->view().row(0).front() == 1.0F);
    CHECK(output->view().row(1).front() == untouched);
}
namespace {
std::atomic<unsigned> completed_items{0};
constexpr unsigned final_items = 8;
bool stop_after_completion(core::Checkpoint /*phase*/) noexcept {
    return completed_items.load() == final_items;
}
} // namespace
TEST_CASE("A terminal stop observation cannot cancel already completed work", "[cancellation]") {
    for (const unsigned count : {1U, 2U, 4U}) {
        completed_items.store(0);
        const exec::Scheduler scheduler{workers(count),
                                        core::Cancellation{{}, stop_after_completion}};
        const auto task = [](std::size_t) -> core::Result<void> {
            completed_items.fetch_add(1);
            return {};
        };
        CHECK(scheduler.for_each(final_items, exec::WorkRef{task}));
        CHECK(completed_items.load() == final_items);
    }
}
TEST_CASE("A stop requested by the final completed task has worker-independent success",
          "[cancellation]") {
    for (const unsigned count : {1U, 2U, 4U}) {
        std::stop_source source;
        std::atomic<unsigned> finished{0};
        const exec::Scheduler scheduler{workers(count), core::Cancellation{source.get_token()}};
        const auto task = [&](std::size_t) -> core::Result<void> {
            if (finished.fetch_add(1) + 1 == final_items) {
                request_stop(source);
            }
            return {};
        };
        CHECK(scheduler.for_each(final_items, exec::WorkRef{task}));
        CHECK(finished.load() == final_items);
        CHECK(source.stop_requested());
    }
}
} // namespace docenhance::tests
