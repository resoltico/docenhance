// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace docenhance::app {
struct Invocation;
// Per-call execution control is deliberately absent from Plan, ProcessRequest and run records.
class ExecutionControl {
  public:
    ExecutionControl() = default;
    [[nodiscard]] static core::Result<ExecutionControl>
    create(std::optional<unsigned> workers = core::default_execution_threads,
           std::uint64_t memory_bytes = core::default_memory_limit,
           core::Cancellation cancellation = {});
    [[nodiscard]] std::optional<unsigned> workers() const noexcept {
        return workers_;
    }
    [[nodiscard]] std::size_t memory_limit() const noexcept {
        return memory_limit_;
    }
    [[nodiscard]] core::Cancellation cancellation() const noexcept {
        return cancellation_;
    }

  private:
    std::optional<unsigned> workers_ = core::default_execution_threads;
    std::size_t memory_limit_ = core::default_memory_limit;
    core::Cancellation cancellation_;
};
[[nodiscard]] core::Result<ExecutionControl>
prepare_execution(const Invocation& invocation, core::Cancellation cancellation = {});
} // namespace docenhance::app
