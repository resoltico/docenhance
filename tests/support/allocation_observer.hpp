// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <cstddef>
#include <span>
#ifdef DE_ALLOCATION_C_OBSERVATION
#include "fft_allocation_trace.hpp"

#include <cerrno>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <utility>
#endif
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
[[nodiscard]] void* original_allocate(std::size_t bytes) noexcept(
    noexcept(std::declval<decltype(&std::malloc)>()(bytes)));
void original_release(void* pointer) noexcept(
    noexcept(std::declval<decltype(&std::free)>()(pointer)));
[[nodiscard]] void*
c_allocate(std::size_t bytes) noexcept(noexcept(std::declval<decltype(&std::malloc)>()(bytes)));
void c_release(void* pointer) noexcept(noexcept(std::declval<decltype(&std::free)>()(pointer)));
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
