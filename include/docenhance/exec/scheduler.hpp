// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"

#include <concepts>
#include <cstddef>
#include <memory>

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
//   same failure      When several items fail, the failure reported is the one with the lowest
//                     index, not the one that happened to be noticed first.
//   no allocation     Workers and their result slots are fixed-size and live on the stack, so a
//                     schedule cannot compete with the page for the memory budget.

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
    explicit Scheduler(Concurrency concurrency) noexcept : concurrency_(concurrency) {}

    // Runs work(index) for every index below count and returns the lowest-index failure, or
    // success. With one worker the work runs on the calling thread and no thread is created.
    // A failure stops the remaining items, which is why an item must be independent of the rest.
    [[nodiscard]] core::Result<void> for_each(std::size_t count, WorkRef work) const;
    [[nodiscard]] unsigned workers() const noexcept {
        return concurrency_.workers();
    }

  private:
    Concurrency concurrency_;
};
} // namespace docenhance::exec
