// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/exec/scheduler.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <expected>
#include <functional>
#include <new>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

namespace docenhance::exec {
namespace {
// Worker exception handlers do not allocate; materialize diagnostics after every worker joins.
struct Outcome {
    std::size_t index = 0;
    std::optional<core::Error> error;
    std::optional<core::ErrorCode> exception;
};
void perform(const WorkRef& work, std::size_t index, Outcome& outcome) noexcept {
    outcome.index = index;
    try {
        auto result = work(index);
        if (!result) {
            outcome.error = std::move(result.error());
        }
    } catch (const std::bad_alloc&) {
        outcome.exception = core::ErrorCode::resource;
    } catch (...) {
        outcome.exception = core::ErrorCode::invariant;
    }
}
[[nodiscard]] bool failed(const Outcome& outcome) noexcept {
    return outcome.error.has_value() || outcome.exception.has_value();
}
[[nodiscard]] bool cancelled(const Outcome& outcome) noexcept {
    return outcome.error && outcome.error->code == core::ErrorCode::cancelled;
}
struct WorkState {
    std::reference_wrapper<const WorkRef> work;
    std::reference_wrapper<const core::Cancellation> cancellation;
    std::size_t count;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    std::atomic<bool> interrupted{false};

    [[nodiscard]] bool observe_stop() noexcept {
        if (!cancellation.get().requested(core::Checkpoint::scheduling)) {
            return false;
        }
        interrupted.store(true, std::memory_order_relaxed);
        stop.store(true, std::memory_order_relaxed);
        return true;
    }
    // Count may be SIZE_MAX; claiming never increments past count or wraps.
    [[nodiscard]] std::optional<std::size_t> claim() noexcept {
        auto index = next.load(std::memory_order_relaxed);
        while (index != count) {
            if (next.compare_exchange_weak(index, index + 1, std::memory_order_relaxed)) {
                return index;
            }
        }
        return std::nullopt;
    }
};
void work_through(WorkState& state, Outcome& outcome) noexcept {
    while (!state.stop.load(std::memory_order_relaxed) && !state.observe_stop()) {
        const auto index = state.claim();
        if (!index) {
            return;
        }
        perform(state.work.get(), *index, outcome);
        if (failed(outcome)) {
            state.stop.store(true, std::memory_order_relaxed);
            return;
        }
    }
}
[[nodiscard]] core::Result<void> result_of(Outcome& outcome) {
    if (outcome.error) {
        return std::unexpected(std::move(*outcome.error));
    }
    if (outcome.exception) {
        return core::failure(*outcome.exception,
                             *outcome.exception == core::ErrorCode::resource
                                 ? "A worker exhausted a system resource"
                                 : "A worker violated the non-throwing task contract");
    }
    return {};
}
[[nodiscard]] core::Result<void> earliest(std::array<Outcome, max_workers>& outcomes,
                                          unsigned workers, bool interrupted) {
    Outcome* first = nullptr;
    for (unsigned worker = 0; worker < workers; ++worker) {
        Outcome& outcome = outcomes.at(worker);
        if (cancelled(outcome)) {
            interrupted = true;
        } else if (failed(outcome) && (first == nullptr || outcome.index < first->index)) {
            first = &outcome;
        }
    }
    if (first != nullptr) {
        return result_of(*first);
    }
    return interrupted ? core::Result<void>{core::cancelled()} : core::Result<void>{};
}
core::Result<void> run_here(std::size_t count, const WorkRef& work,
                            const core::Cancellation& cancellation) {
    Outcome outcome;
    for (std::size_t index = 0; index < count; ++index) {
        if (cancellation.requested(core::Checkpoint::scheduling)) {
            return core::cancelled();
        }
        perform(work, index, outcome);
        if (failed(outcome)) {
            return result_of(outcome);
        }
    }
    return {};
}
[[nodiscard]] bool launch(WorkState& state, unsigned workers,
                          std::array<Outcome, max_workers>& outcomes) {
    // Partial launch failure stops assignment and joins every started worker before returning.
    std::array<std::jthread, max_workers> threads{};
    try {
        for (unsigned worker = 0; worker < workers; ++worker) {
            if (state.observe_stop()) {
                break;
            }
            threads.at(worker) =
                std::jthread{[&, worker] { work_through(state, outcomes.at(worker)); }};
        }
    } catch (const std::bad_alloc&) {
        state.stop.store(true, std::memory_order_relaxed);
        return false;
    } catch (const std::system_error&) {
        state.stop.store(true, std::memory_order_relaxed);
        return false;
    }
    return true;
}
core::Result<void> run_on_workers(std::size_t count, const WorkRef& work, unsigned workers,
                                  const core::Cancellation& cancellation) {
    std::array<Outcome, max_workers> outcomes{};
    WorkState state{.work = work, .cancellation = cancellation, .count = count};
    if (!launch(state, workers, outcomes)) {
        return core::failure(core::ErrorCode::resource, "The system refused a worker thread");
    }
    return earliest(outcomes, workers, state.interrupted.load(std::memory_order_relaxed));
}
} // namespace
core::Result<void> Scheduler::for_each(std::size_t count, WorkRef work) const {
    if (count == 0) {
        return {};
    }
    const auto workers =
        static_cast<unsigned>(std::min(count, static_cast<std::size_t>(concurrency_.workers())));
    return workers == 1 ? run_here(count, work, cancellation_)
                        : run_on_workers(count, work, workers, cancellation_);
}
} // namespace docenhance::exec
