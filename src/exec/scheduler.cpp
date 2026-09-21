// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/exec/scheduler.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <thread>
#include <utility>

namespace docenhance::exec {
namespace {
// What one worker took away: the lowest index it failed on, and why.
struct Outcome {
    std::size_t index = 0;
    std::optional<core::Error> error;
};

// Items are handed out one at a time from a shared counter. For tiles of roughly equal cost this
// balances as well as stealing does, and it keeps the scheduler small enough to reason about.
void work_through(std::atomic<std::size_t>& next, std::size_t count, std::atomic<bool>& stop,
                  const WorkRef& work, Outcome& outcome) {
    while (!stop.load(std::memory_order_relaxed)) {
        const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= count) {
            return;
        }
        auto result = work(index);
        if (!result) {
            outcome = {.index = index, .error = std::move(result.error())};
            // The rest of the page is pointless once one item has failed.
            stop.store(true, std::memory_order_relaxed);
            return;
        }
    }
}

// The failure a sequential run would have reported: the one with the lowest index.
[[nodiscard]] core::Result<void> earliest(const std::array<Outcome, max_workers>& outcomes,
                                          unsigned workers) {
    const Outcome* first = nullptr;
    for (unsigned worker = 0; worker < workers; ++worker) {
        const Outcome& outcome = outcomes.at(worker);
        if (outcome.error.has_value() && (first == nullptr || outcome.index < first->index)) {
            first = &outcome;
        }
    }
    if (first == nullptr || !first->error.has_value()) {
        return {};
    }
    const core::Error& error = *first->error;
    return core::failure(error.code, error.message);
}

// One worker means no worker: the work runs where it was asked for, which is what the fuzzers,
// the sanitizers and a reproduction run all want.
core::Result<void> run_here(std::size_t count, const WorkRef& work) {
    for (std::size_t index = 0; index < count; ++index) {
        auto result = work(index);
        if (!result) {
            return result;
        }
    }
    return {};
}

// Workers and their result slots are fixed-size and live here, so a schedule allocates nothing
// and cannot compete with the page for the memory budget.
core::Result<void> run_on_workers(std::size_t count, const WorkRef& work, unsigned workers) {
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    std::array<Outcome, max_workers> outcomes{};
    {
        // Threads join where they are declared, so no path leaves a worker running.
        std::array<std::jthread, max_workers> threads{};
        for (unsigned worker = 0; worker < workers; ++worker) {
            threads.at(worker) = std::jthread{
                [&, worker] { work_through(next, count, stop, work, outcomes.at(worker)); }};
        }
    }
    return earliest(outcomes, workers);
}
} // namespace

core::Result<void> Scheduler::for_each(std::size_t count, WorkRef work) const {
    if (count == 0) {
        return {};
    }
    const unsigned workers = concurrency_.workers();
    return concurrency_.sequential() ? run_here(count, work) : run_on_workers(count, work, workers);
}
} // namespace docenhance::exec
