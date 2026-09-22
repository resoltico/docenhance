// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"

#include "docenhance/core/result.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <string>

namespace docenhance::core {
struct BudgetState {
    std::atomic<std::size_t> references{1};
    std::atomic<std::size_t> used{0};
};
namespace {
void release_state(BudgetState* const state) noexcept {
    if (state != nullptr && state->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        const std::unique_ptr<BudgetState> last_owner{state};
    }
}
// The one place in the project that asks the system for memory. Everything else takes a Buffer.
[[nodiscard]] std::byte* acquire(std::size_t bytes) noexcept {
    return static_cast<std::byte*>(
        ::operator new(bytes, std::align_val_t{buffer_alignment}, std::nothrow));
}
void discard(std::byte* const data) noexcept {
    ::operator delete(data, std::align_val_t{buffer_alignment}, std::nothrow);
}
std::string mebibytes(std::size_t bytes) {
    constexpr std::size_t per_mebibyte = std::size_t{1024} * 1024;
    return std::to_string(bytes / per_mebibyte) + " MiB";
}
} // namespace

void Buffer::release() noexcept {
    if (data_ != nullptr) {
        discard(data_);
    }
    if (state_ != nullptr) {
        state_->used.fetch_sub(size_, std::memory_order_acq_rel);
        release_state(state_);
    }
    data_ = nullptr;
    size_ = 0;
    state_ = nullptr;
}

Budget::Budget(std::size_t limit) noexcept
    : limit_(limit), state_(new (std::nothrow) BudgetState{}) {}

Budget::~Budget() {
    release_state(state_);
}

std::size_t Budget::used() const noexcept {
    return state_ == nullptr ? 0 : state_->used.load(std::memory_order_relaxed);
}

bool Budget::charge(std::size_t bytes) noexcept {
    if (state_ == nullptr) {
        return false;
    }
    std::size_t used = state_->used.load(std::memory_order_relaxed);
    while (true) {
        if (bytes > limit_ - used) {
            return false;
        }
        if (state_->used.compare_exchange_weak(used, used + bytes, std::memory_order_acq_rel,
                                               std::memory_order_relaxed)) {
            return true;
        }
    }
}

Result<Buffer> Budget::allocate(std::size_t bytes) {
    if (bytes == 0) {
        return Buffer{};
    }
    if (!charge(bytes)) {
        return failure(ErrorCode::resource, "The working-memory budget of " + mebibytes(limit_) +
                                                " cannot hold another " + mebibytes(bytes) + "; " +
                                                mebibytes(available()) + " is available");
    }
    std::byte* const data = acquire(bytes);
    if (data == nullptr) {
        state_->used.fetch_sub(bytes, std::memory_order_acq_rel);
        return failure(ErrorCode::resource,
                       "The system refused an allocation of " + mebibytes(bytes));
    }
    state_->references.fetch_add(1, std::memory_order_relaxed);
    return Buffer{data, bytes, state_};
}
} // namespace docenhance::core
