// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#ifdef DE_ALLOCATION_C_OBSERVATION
#include "fft_allocation_trace.hpp"

#include <cerrno>
#include <cstdlib>
#include <mutex>
#endif
namespace docenhance::tests::observer_diagnostic {
struct Counters {
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> ticks{0};
};
inline Counters lookup{};
inline Counters reservation{};
inline std::atomic<bool> valid{true};
std::uint64_t now() noexcept;
std::uint64_t frequency() noexcept;
class Timer {
  public:
    explicit Timer(Counters& counters) noexcept : counters_(counters), began_(now()) {}
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;
    ~Timer() {
        const auto ended = now();
        if (ended < began_) {
            valid.store(false);
        } else {
            counters_.get().ticks.fetch_add(ended - began_, std::memory_order_relaxed);
        }
        counters_.get().calls.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    std::reference_wrapper<Counters> counters_;
    std::uint64_t began_;
};
struct Snapshot {
    std::uint64_t wall = 0, calls = 0, ticks = 0, reserve_calls = 0, reserve_ticks = 0;
};
inline Snapshot snapshot() noexcept {
    return {
        .wall = now(),
        .calls = lookup.calls.load(),
        .ticks = lookup.ticks.load(),
        .reserve_calls = reservation.calls.load(),
        .reserve_ticks = reservation.ticks.load(),
    };
}
inline void report_since(const char* const phase, const Snapshot before = {}) {
    const auto after = snapshot();
    const auto ticks_per_second = frequency();
    std::cerr << "DIAG original196_timing_only phase=" << phase
              << " kind=" << (before.wall == 0 ? "cumulative_qpc" : "actual_call_delta")
              << " wall_ticks=" << after.wall - before.wall
              << " lookup_calls=" << after.calls - before.calls
              << " lookup_ticks=" << after.ticks - before.ticks
              << " reserve_calls=" << after.reserve_calls - before.reserve_calls
              << " reserve_ticks=" << after.reserve_ticks - before.reserve_ticks
              << " frequency=" << ticks_per_second << " valid=" << (valid.load() ? 1 : 0)
              << " sum_across_threads=1 reserve_includes_lock_wait=1 "
                 "lookup_excludes_caller_lock_wait=1\n";
    std::cerr.flush();
}
template <class Function> bool phase(const char* const name, Function function) {
    report_since(name);
    const bool result = function();
    report_since(result ? "phase.returned.true" : "phase.returned.false");
    return result;
}
} // namespace docenhance::tests::observer_diagnostic

namespace docenhance::tests::allocation_observer {
// A single serial observation owns these counters; native workers finish before they are read.
extern std::atomic<bool> observing;
// A failed native-effect observation is permanent for this instrumentation epoch.
[[nodiscard]] bool observation_valid() noexcept;
extern std::atomic<std::size_t> live;
extern std::atomic<std::size_t> peak;
extern std::atomic<std::size_t> allocation_attempts;
extern std::atomic<std::size_t> c_allocation_attempts;
extern std::atomic<std::size_t> failure_at;
extern std::atomic<bool> persistent_failure;
extern std::size_t worker_count;
[[nodiscard]] bool initialize_hooks() noexcept;
[[nodiscard]] bool refuse_allocation() noexcept;
void acquire_bytes(std::size_t bytes);
void reset_workers() noexcept;
#ifdef DE_ALLOCATION_C_OBSERVATION
// Match declared C function types; /EHc call assumptions are not an exception specification.
template <bool Nothrow, class Return, class... Arguments>
constexpr bool declared_nothrow(Return (* /*function*/)(Arguments...) noexcept(Nothrow)) noexcept {
    return Nothrow;
}
inline constexpr bool malloc_nothrow = declared_nothrow(&std::malloc);
inline constexpr bool free_nothrow = declared_nothrow(&std::free);
[[nodiscard]] void* original_allocate(std::size_t bytes) noexcept(malloc_nothrow);
void original_release(void* pointer) noexcept(free_nothrow);
[[nodiscard]] void* c_allocate(std::size_t bytes) noexcept(malloc_nothrow);
void c_release(void* pointer) noexcept(free_nothrow);
#endif
[[nodiscard]] bool c_contract_cases();
namespace detail {
struct ObservedAllocation {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    bool measured = false;
    bool retiring = false;
};
// Callers hold the actual ledger mutex; retiring owners are not a new pointer lookup's lifetime.
[[nodiscard]] inline ObservedAllocation* find_allocation(std::span<ObservedAllocation> slots,
                                                         const void* const pointer) noexcept {
    const observer_diagnostic::Timer diagnostic{observer_diagnostic::lookup};
    for (auto& slot : slots) {
        if (pointer != nullptr && slot.pointer == pointer && !slot.retiring) {
            return &slot;
        }
    }
    return nullptr;
}
#ifdef DE_ALLOCATION_C_OBSERVATION
// The slot stays occupied, but not discoverable by a later allocation reusing its address.
class CReleaseReservation {
  public:
    CReleaseReservation(std::span<ObservedAllocation> slots, std::mutex& lock, const void* pointer,
                        std::atomic<bool>& health) noexcept
        : mutex_(lock), valid_(health) {
        try {
            const std::scoped_lock held{mutex_.get()};
            record_ = find_allocation(slots, pointer);
            if (record_ != nullptr) {
                record_->retiring = true;
            }
        } catch (...) {
            std::abort();
        }
    }
    ~CReleaseReservation() {
        if (completed_) {
            return;
        }
        const int provider_errno = errno;
        // Unknown native effect is not a safe retry: keep any old slot quarantined.
        valid_.get().store(false);
        errno = provider_errno;
    }
    CReleaseReservation(const CReleaseReservation&) = delete;
    CReleaseReservation& operator=(const CReleaseReservation&) = delete;
    CReleaseReservation(CReleaseReservation&&) = delete;
    CReleaseReservation& operator=(CReleaseReservation&&) = delete;
    void complete() noexcept {
        try {
            const std::scoped_lock held{mutex_.get()};
            if (record_ != nullptr) {
                if (record_->measured) {
                    live.fetch_sub(record_->bytes);
                    fft_allocation_trace::record(record_->bytes,
                                                 fft_allocation_trace::Kind::released);
                }
                *record_ = {};
                record_ = nullptr;
            }
            // Successful null/unrecorded releases are confirmed too; health never resets.
            completed_ = true;
        } catch (...) {
            std::abort();
        }
    }

  private:
    std::reference_wrapper<std::mutex> mutex_;
    std::reference_wrapper<std::atomic<bool>> valid_;
    ObservedAllocation* record_ = nullptr;
    bool completed_ = false;
};
#endif
} // namespace detail
} // namespace docenhance::tests::allocation_observer
