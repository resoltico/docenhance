// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <utility>

namespace docenhance::exec {
// The schedule, kept apart from the algorithm. A kernel is a pure function of the region it is
// given; this decides how many of those run at once, and it is the only place in the project that
// starts a thread.
//
// Three promises make a parallel run as trustworthy as a sequential one:
//
//   same answers      The partition is chosen before the workers are, and an item may touch only
//                     the data its own index owns, so the result never depends on the worker count
//                     or on who finished first. One worker is not a different program.
//   same failure      Among observed genuine errors, report the lowest task index; cancellation
//                     cannot erase one. Skipped unfinished work returns cancellation, not success.
//   bounded resources Worker metadata has a fixed ceiling. Thread stacks and OS bookkeeping
//                     are outside the image budget; thread launch failure is a resource error.
//                     Task exceptions become failures, never an uncaught worker termination.

// A non-owning reference to the work. It never allocates and never outlives the call it is passed
// to, which is why it takes the callable by reference.
class WorkRef {
  public:
    template <typename Callable>
        requires std::invocable<const Callable&, std::size_t> &&
                     std::same_as<std::invoke_result_t<const Callable&, std::size_t>,
                                  core::Result<void>>
    explicit WorkRef(const Callable& callable) noexcept
        : object_(std::addressof(callable)), invoke_([](const void* object, std::size_t index) {
              return (*static_cast<const Callable*>(object))(index);
          }) {}

    [[nodiscard]] core::Result<void> operator()(std::size_t index) const {
        return invoke_(object_, index);
    }

  private:
    const void* object_;
    core::Result<void> (*invoke_)(const void*, std::size_t);
};

class Scheduler {
  public:
    explicit Scheduler(Concurrency concurrency, core::Cancellation cancellation = {}) noexcept
        : concurrency_(concurrency), cancellation_(std::move(cancellation)) {}

    // Runs independent work items until completion, error or observed cancellation. Every started
    // worker joins; genuine errors outrank cancellation. Empty work may succeed. With one worker
    // work runs inline. An item must not depend on another item being scheduled or completing.
    [[nodiscard]] core::Result<void> for_each(std::size_t count, WorkRef work) const;
    [[nodiscard]] unsigned workers() const noexcept {
        return concurrency_.workers();
    }

    [[nodiscard]] const core::Cancellation& cancellation() const noexcept {
        return cancellation_;
    }

  private:
    Concurrency concurrency_;
    core::Cancellation cancellation_;
};
} // namespace docenhance::exec
