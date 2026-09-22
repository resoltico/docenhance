// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/exec/scheduler.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <expected>
#include <new>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

namespace docenhance::exec {
namespace {
// Failure metadata is bounded. Exception handlers do not allocate diagnostic strings: those
// are constructed on the caller only after every started worker has joined.
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

// A bounded compare/exchange counter cannot wrap even when count is SIZE_MAX.
[[nodiscard]] std::optional<std::size_t> claim(std::atomic<std::size_t>& next,
                                               std::size_t count) noexcept {
    std::size_t index = next.load(std::memory_order_relaxed);
    while (index != count) {
        if (next.compare_exchange_weak(index, index + 1, std::memory_order_relaxed)) {
            return index;
        }
    }
    return std::nullopt;
}
void work_through(std::atomic<std::size_t>& next, std::size_t count, std::atomic<bool>& stop,
                  const WorkRef& work, Outcome& outcome) noexcept {
    while (!stop.load(std::memory_order_relaxed)) {
        const auto index = claim(next, count);
        if (!index) {
            return;
        }
        perform(work, *index, outcome);
        if (failed(outcome)) {
            stop.store(true, std::memory_order_relaxed);
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
                                          unsigned workers) {
    Outcome* first = nullptr;
    for (unsigned worker = 0; worker < workers; ++worker) {
        Outcome& outcome = outcomes.at(worker);
        if (failed(outcome) && (first == nullptr || outcome.index < first->index)) {
            first = &outcome;
        }
    }
    return first == nullptr ? core::Result<void>{} : result_of(*first);
}

core::Result<void> run_here(std::size_t count, const WorkRef& work) {
    Outcome outcome;
    for (std::size_t index = 0; index < count; ++index) {
        perform(work, index, outcome);
        if (failed(outcome)) {
            return result_of(outcome);
        }
    }
    return {};
}

[[nodiscard]] bool launch(std::size_t count, const WorkRef& work, unsigned workers,
                          std::array<Outcome, max_workers>& outcomes) {
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    // Constructed outside try; partial launch failures stop and join every started worker.
    std::array<std::jthread, max_workers> threads{};
    try {
        for (unsigned worker = 0; worker < workers; ++worker) {
            threads.at(worker) = std::jthread{
                [&, worker] { work_through(next, count, stop, work, outcomes.at(worker)); }};
        }
    } catch (const std::bad_alloc&) {
        stop.store(true, std::memory_order_relaxed);
        return false;
    } catch (const std::system_error&) {
        stop.store(true, std::memory_order_relaxed);
        return false;
    }
    return true; // threads are joined before the caller inspects any Outcome.
}
core::Result<void> run_on_workers(std::size_t count, const WorkRef& work, unsigned workers) {
    std::array<Outcome, max_workers> outcomes{};
    if (!launch(count, work, workers, outcomes)) {
        return core::failure(core::ErrorCode::resource, "The system refused a worker thread");
    }
    return earliest(outcomes, workers);
}
} // namespace

core::Result<void> Scheduler::for_each(std::size_t count, WorkRef work) const {
    if (count == 0) {
        return {};
    }
    const auto workers =
        static_cast<unsigned>(std::min(count, static_cast<std::size_t>(concurrency_.workers())));
    return workers == 1 ? run_here(count, work) : run_on_workers(count, work, workers);
}
} // namespace docenhance::exec
