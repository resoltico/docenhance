// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"

#include "docenhance/core/result.hpp"

#include <atomic>
#include <cstddef>
#include <new>
#include <string>

namespace docenhance::core {
namespace {
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
    if (budget_ != nullptr) {
        budget_->refund(size_);
    }
    data_ = nullptr;
    size_ = 0;
    budget_ = nullptr;
}

bool Budget::charge(std::size_t bytes) noexcept {
    std::size_t used = used_.load(std::memory_order_relaxed);
    while (true) {
        if (bytes > limit_ - used) {
            return false;
        }
        if (used_.compare_exchange_weak(used, used + bytes, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
            return true;
        }
    }
}

void Budget::refund(std::size_t bytes) noexcept {
    used_.fetch_sub(bytes, std::memory_order_acq_rel);
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
        refund(bytes);
        return failure(ErrorCode::resource,
                       "The system refused an allocation of " + mebibytes(bytes));
    }
    return Buffer{data, bytes, this};
}
} // namespace docenhance::core
