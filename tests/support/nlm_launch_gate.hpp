// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "allocation_observer.hpp"
#include "docenhance/core/cancellation.hpp"
#include "require.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <stdexcept>
#include <thread>

namespace docenhance::tests {
// The probe holds workers before WorkRef execution, never between dependent work items.
class NlmLaunchGate {
  public:
    explicit NlmLaunchGate(unsigned workers,
                           std::chrono::milliseconds bound = std::chrono::seconds{2})
        : caller_(std::this_thread::get_id()), bound_(bound), parallel_(workers == 2) {
        require(active == nullptr, "one serial NLM launch observation owns its probe");
        if (parallel_) {
            // Watchdog startup and its readiness handshake precede allocation observation.
            watchdog_ = std::thread{[this] { watch(); }};
            ready_.wait(false);
            {
                const std::scoped_lock lock{mutex_};
                boot_wake_ = true;
                condition_.notify_all();
            }
            boot_ready_.wait(false);
        }
        active = this;
    }
    ~NlmLaunchGate() {
        finish();
    }
    NlmLaunchGate(const NlmLaunchGate&) = delete;
    NlmLaunchGate& operator=(const NlmLaunchGate&) = delete;
    NlmLaunchGate(NlmLaunchGate&&) = delete;
    NlmLaunchGate& operator=(NlmLaunchGate&&) = delete;
    static bool observe(core::Checkpoint phase) noexcept {
        if (phase == core::Checkpoint::scheduling && active != nullptr) {
            active->scheduling();
        }
        return false;
    }
    void finish() {
        {
            const std::scoped_lock lock{mutex_};
            finished_ = true;
            released_.store(true);
            condition_.notify_all();
        }
        if (watchdog_.joinable()) {
            watchdog_.join();
        }
        if (active == this) {
            active = nullptr;
        }
    }
    [[nodiscard]] bool valid() const noexcept {
        return !expired_.load();
    }
    [[nodiscard]] std::size_t arrivals(std::size_t phase) const noexcept {
        if (phase >= arrivals_.size()) {
            std::terminate();
        }
        return std::span{arrivals_}.subspan(phase, 1).front().load();
    }
    void require_valid() const {
        require(valid(), "NLM launch watchdog use refuses resource qualification");
    }

  private:
    void release() {
        const std::scoped_lock lock{mutex_};
        released_.store(true);
        armed_ = false;
        condition_.notify_all();
    }
    void scheduling() {
        if (!parallel_) {
            return;
        }
        if (std::this_thread::get_id() == caller_) {
            // Main-thread visits are eligibility and weights (both serial), then gather and
            // mapping.
            ++visits_;
            const std::scoped_lock lock{mutex_};
            enabled_.store(visits_ == 3 || visits_ == 4);
            phase_.store(visits_ == 3 ? 0U : 1U);
            released_.store(!enabled_.load());
            armed_ = false;
            ++generation_;
            condition_.notify_all();
            return;
        }
        if (!enabled_.load() || released_.load()) {
            return;
        }
        const auto phase = phase_.load();
        if (phase >= arrivals_.size()) {
            std::terminate();
        }
        auto& arrivals = std::span{arrivals_}.subspan(phase, 1).front();
        const auto arrived = arrivals.fetch_add(1) + 1;
        arrivals.notify_all();
        if (arrived >= 2) {
            release();
            return;
        }
        {
            const std::scoped_lock lock{mutex_};
            if (!released_.load()) {
                expires_ = std::chrono::steady_clock::now() + bound_;
                armed_ = true;
                condition_.notify_all();
            }
        }
        while (!released_.load()) {
            const auto failure = allocation_observer::failure_at.load();
            if (failure != 0 && allocation_observer::allocation_attempts.load() >= failure) {
                // Refusal of a second launch must release the first worker before joining it.
                release();
            } else {
                std::this_thread::yield();
            }
        }
    }
    void watch() {
        std::unique_lock lock{mutex_};
        ready_.store(true);
        ready_.notify_all();
        condition_.wait(lock, [this] { return boot_wake_; });
        // Prime this condition variable's real wake and timed-wait paths before observation.
        static_cast<void>(
            condition_.wait_until(lock, std::chrono::steady_clock::now(), [] { return false; }));
        boot_ready_.store(true);
        boot_ready_.notify_all();
        while (!finished_) {
            condition_.wait(lock, [this] { return finished_ || armed_; });
            if (finished_) {
                return;
            }
            const auto generation = generation_;
            if (!condition_.wait_until(lock, expires_, [this, generation] {
                    return finished_ || !armed_ || released_.load() || generation_ != generation;
                })) {
                // Unexpected native launch failure is adverse, never a passing injected case.
                expired_.store(true);
                released_.store(true);
                armed_ = false;
            }
        }
    }
    static inline NlmLaunchGate* active = nullptr;
    const std::thread::id caller_;
    const std::chrono::milliseconds bound_;
    const bool parallel_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread watchdog_;
    std::array<std::atomic<std::size_t>, 2> arrivals_{};
    std::atomic<std::size_t> phase_{0};
    std::atomic<bool> enabled_{false}, released_{true}, expired_{false}, ready_{false},
        boot_ready_{false};
    std::chrono::steady_clock::time_point expires_;
    std::size_t visits_ = 0, generation_ = 0;
    bool armed_ = false, finished_ = false, boot_wake_ = false;
};
inline void nlm_launch_gate_controls() {
    namespace allocation = allocation_observer;
    // A held worker must leave after the actual injection ordinal is attained.
    {
        NlmLaunchGate gate{2};
        static_cast<void>(NlmLaunchGate::observe(core::Checkpoint::scheduling));
        static_cast<void>(NlmLaunchGate::observe(core::Checkpoint::scheduling));
        static_cast<void>(NlmLaunchGate::observe(core::Checkpoint::scheduling));
        allocation::failure_at.store(1);
        allocation::allocation_attempts.store(0);
        std::thread worker{
            [] { static_cast<void>(NlmLaunchGate::observe(core::Checkpoint::scheduling)); }};
        while (gate.arrivals(0) == 0) {
            std::this_thread::yield();
        }
        using New = void* (*)(std::size_t);
        using Delete = void (*)(void*) noexcept;
        const volatile auto allocate = static_cast<New>(&::operator new);
        const auto release = static_cast<Delete>(&::operator delete);
        std::unique_ptr<void, Delete> bytes{nullptr, release};
        bool refused = false;
        allocation::observing.store(true);
        try {
            bytes.reset(allocate(211));
        } catch (const std::bad_alloc&) {
            refused = true;
        }
        allocation::observing.store(false);
        bytes.reset();
        worker.join();
        gate.finish();
        gate.require_valid();
        require(refused && allocation::allocation_attempts.load() == 1 &&
                    allocation::live.load() == 0 && gate.arrivals(0) == 1,
                "actual ordinary allocation refusal releases a singly held launch worker");
        allocation::failure_at.store(0);
        allocation::allocation_attempts.store(0);
    }
    // Expire the actual watchdog path without sleeps or allowing expiry to qualify.
    {
        NlmLaunchGate gate{2, std::chrono::milliseconds{0}};
        static_cast<void>(NlmLaunchGate::observe(core::Checkpoint::scheduling));
        static_cast<void>(NlmLaunchGate::observe(core::Checkpoint::scheduling));
        static_cast<void>(NlmLaunchGate::observe(core::Checkpoint::scheduling));
        std::thread worker{
            [] { static_cast<void>(NlmLaunchGate::observe(core::Checkpoint::scheduling)); }};
        worker.join();
        gate.finish();
        bool refused = false;
        try {
            gate.require_valid();
        } catch (const std::runtime_error&) {
            refused = true;
        }
        require(
            refused && !gate.valid() && gate.arrivals(0) == 1,
            "actual watchdog releases a held worker and its shared qualification guard refuses");
    }
}
} // namespace docenhance::tests
