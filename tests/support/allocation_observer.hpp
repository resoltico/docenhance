// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstddef>
namespace docenhance::tests::allocation_observer {
// A single serial observation owns these counters; native workers finish before they are read.
extern std::atomic<bool> observing;
extern std::atomic<std::size_t> live;
extern std::atomic<std::size_t> peak;
extern std::atomic<std::size_t> allocation_attempts;
extern std::atomic<std::size_t> failure_at;
extern std::size_t worker_count;
[[nodiscard]] bool initialize_hooks() noexcept;
[[nodiscard]] bool refuse_allocation() noexcept;
void acquire_bytes(std::size_t bytes);
} // namespace docenhance::tests::allocation_observer
