// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <cstddef>
namespace docenhance::tests::fft_allocation_gate {
inline constexpr unsigned worker_limit = 7;
extern std::atomic<unsigned> arrivals;
extern std::atomic<bool> released;
extern std::atomic<bool> failed;
void arm(std::size_t bytes) noexcept;
void allocated(std::size_t bytes) noexcept;
void finish() noexcept;
void disable() noexcept;
} // namespace docenhance::tests::fft_allocation_gate
