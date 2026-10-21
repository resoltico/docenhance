// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
namespace docenhance::tests {
class BinaryParticipation {
  public:
    BinaryParticipation(unsigned actual_workers, std::size_t actual_items) noexcept {
        auto& value = state();
        value.caller = std::this_thread::get_id();
        value.active_bound = std::min(std::size_t{actual_workers}, actual_items);
        value.observed.store(0);
        value.caller_observed.store(false);
        value.ignored_caller.store(0);
        value.generation.fetch_add(1);
    }
    [[nodiscard]] core::Cancellation cancellation() const noexcept {
        return cancellation_;
    }
    [[nodiscard]] static unsigned observed() noexcept {
        return state().observed.load();
    }
    [[nodiscard]] static bool caller_observed() noexcept {
        return state().caller_observed.load();
    }
    [[nodiscard]] static std::size_t active_bound() noexcept {
        return state().active_bound;
    }

  private:
    core::Cancellation cancellation_{{}, observe};
    struct State {
        std::thread::id caller;
        std::size_t active_bound = 1;
        std::atomic<unsigned> observed{0};
        std::atomic<unsigned> ignored_caller{0};
        std::atomic<bool> caller_observed{false};
        std::atomic<std::uint64_t> generation{0};
    };
    static State& state() noexcept {
        static State value;
        return value;
    }
    static bool observe(core::Checkpoint phase) noexcept {
        if (phase != core::Checkpoint::processing) {
            return false;
        }
        auto& value = state();
        const bool caller = std::this_thread::get_id() == value.caller;
        if (value.active_bound > 1 && caller) {
            value.ignored_caller.fetch_add(1);
            return false;
        }
        thread_local std::uint64_t generation = 0;
        if (generation != value.generation.load()) {
            generation = value.generation.load();
            value.observed.fetch_add(1);
            if (caller) {
                value.caller_observed.store(true);
            }
        }
        return false;
    }
};
} // namespace docenhance::tests
