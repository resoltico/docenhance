// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#ifndef _WIN32
#include <array>
#include <signal.h> // NOLINT(modernize-deprecated-headers): POSIX sigaction provider.
#endif

namespace docenhance::entry {
// Standalone process adapter, installed once before dispatch and removed after response delivery.
// Handlers only touch a static, monotonic, lock-free latch; no callback borrows this scope.
class InterruptScope {
  public:
    InterruptScope() = default;
    InterruptScope(const InterruptScope&) = delete;
    InterruptScope& operator=(const InterruptScope&) = delete;
    InterruptScope(InterruptScope&&) = delete;
    InterruptScope& operator=(InterruptScope&&) = delete;
    ~InterruptScope();
    [[nodiscard]] bool install() noexcept;

  private:
#ifdef _WIN32
    bool installed_ = false;
#else
    std::array<struct sigaction, 2> previous_{};
    std::array<bool, 2> installed_{};
#endif
};
[[nodiscard]] core::Cancellation process_cancellation() noexcept;
} // namespace docenhance::entry
