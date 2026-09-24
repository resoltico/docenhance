// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"

#include <atomic>
#include <cstddef>

namespace docenhance::tests {
// One synchronized fixture per test process; reset only before launching any tested workers.
// No data pointer enters the production capability and no test code runs from an OS handler.
class CheckpointStop {
  public:
    CheckpointStop(core::Checkpoint phase, std::size_t allowed) noexcept {
        auto& value = state();
        value.phase = phase;
        value.allowed = allowed;
        value.visits.store(0);
        value.stopped.store(false);
    }
    [[nodiscard]] core::Cancellation cancellation() const noexcept {
        return cancellation_;
    }
    [[nodiscard]] static bool stopped() noexcept {
        return state().stopped.load();
    }
    [[nodiscard]] static std::size_t visits() noexcept {
        return state().visits.load();
    }

  private:
    core::Cancellation cancellation_{{}, observe};
    struct State {
        core::Checkpoint phase = core::Checkpoint::admission;
        std::size_t allowed = 0;
        std::atomic<std::size_t> visits{0};
        std::atomic<bool> stopped{false};
    };
    static State& state() noexcept {
        static State value;
        return value;
    }
    static bool observe(core::Checkpoint at) noexcept {
        auto& value = state();
        if (at == value.phase && value.visits.fetch_add(1) >= value.allowed) {
            value.stopped.store(true);
        }
        return value.stopped.load();
    }
};
} // namespace docenhance::tests
