// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <optional>

namespace docenhance::exec {
// How many workers one page is processed with. Three things decide it, in this order:
//
//   the request     `--threads`: a number the caller chose, or "auto"
//   the machine     how many hardware threads there are, asked for once and passed in
//   the budget      how much working memory a worker needs, and how much there is
//
// The budget has the last word. A worker that cannot be given its working set would either fail
// mid-page or push the process into swap, so the count is reduced until the memory fits, and a
// budget too small for even one worker is a refusal rather than a gamble.
//
// This is not OpenCV's thread count. OpenCV's own parallel_for_ cannot be pinned on every platform
// (macOS dispatches through Grand Central Dispatch, which ignores a requested count), so the
// --threads contract is honoured here, over the tiles of one page, and never delegated.

// The contract's bounds: `auto`, or an integer in [1, 64].
inline constexpr unsigned min_workers = 1;
inline constexpr unsigned max_workers = 64;
// `auto` is deliberately modest: four workers saturate memory bandwidth on a page long before they
// saturate a large machine, and a smaller count keeps the working set smaller.
inline constexpr unsigned automatic_workers = 4;

class Concurrency {
  public:
    // requested: the parsed `--threads` value, or nothing for `auto`.
    // hardware: what the machine reports, which only `auto` uses.
    // available_bytes / per_worker_bytes: the budget, and what one worker needs of it. A
    // per-worker size of zero means the work is not memory-bound and only the request applies.
    [[nodiscard]] static core::Result<Concurrency> resolve(std::optional<unsigned> requested,
                                                           unsigned hardware,
                                                           std::size_t available_bytes,
                                                           std::size_t per_worker_bytes);
    [[nodiscard]] unsigned workers() const noexcept {
        return workers_;
    }
    [[nodiscard]] bool sequential() const noexcept {
        return workers_ == 1;
    }

  private:
    explicit Concurrency(unsigned workers) noexcept : workers_(workers) {}
    unsigned workers_ = 1;
};

// What the machine reports, asked for deliberately and in one place. Every other function takes
// the answer as an argument, so nothing below this layer depends on the machine it runs on.
[[nodiscard]] unsigned detected_concurrency() noexcept;
} // namespace docenhance::exec
