// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/exec/concurrency.hpp"

#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>

namespace docenhance::exec {
namespace {
// What one worker's working set allows, given what is left of the budget.
[[nodiscard]] unsigned affordable(std::size_t available_bytes, std::size_t per_worker_bytes) {
    if (per_worker_bytes == 0) {
        return max_workers;
    }
    const std::size_t fits = available_bytes / per_worker_bytes;
    return static_cast<unsigned>(std::min<std::size_t>(fits, max_workers));
}
} // namespace

core::Result<Concurrency> Concurrency::resolve(std::optional<unsigned> requested, unsigned hardware,
                                               std::size_t available_bytes,
                                               std::size_t per_worker_bytes) {
    const unsigned automatic = std::min(automatic_workers, std::max(min_workers, hardware));
    const unsigned asked = requested.value_or(automatic);
    if (asked < min_workers || asked > max_workers) {
        return core::failure(core::ErrorCode::argument, "A thread count must be between " +
                                                            std::to_string(min_workers) + " and " +
                                                            std::to_string(max_workers));
    }
    const unsigned affordable_workers = affordable(available_bytes, per_worker_bytes);
    if (affordable_workers < min_workers) {
        return core::failure(core::ErrorCode::resource,
                             "The working-memory budget cannot hold even one worker");
    }
    return Concurrency{std::min(asked, affordable_workers)};
}

unsigned detected_concurrency() noexcept {
    const unsigned reported = std::thread::hardware_concurrency();
    return reported == 0 ? min_workers : reported;
}
} // namespace docenhance::exec
