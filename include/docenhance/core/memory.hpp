// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"

#include <atomic>
#include <cstddef>
#include <span>
#include <utility>

namespace docenhance::core {
// A page of a scanned document is large: 40 megapixels of 32-bit samples is 160 MiB per plane, and
// a pipeline holds several at once. Two consequences shape every allocation in this project.
//
// First, running out of memory is an expected outcome of a legitimate request, not a defect, so it
// is a value: buffers are obtained through a Budget that returns Result, and nothing in the
// processing layers allocates through a throwing path.
//
// Second, memory that is not accounted for cannot be limited. `--memory-mib` promises a working
// allocation budget, so every image buffer is charged against one Budget and refunded when the
// buffer dies. A buffer owns its memory alone: it cannot be copied, only moved, so no operation
// silently duplicates a page.

// Rows begin on this boundary so that vector loads on any supported target are aligned.
inline constexpr std::size_t buffer_alignment = 64;

class Budget;

// Owning, aligned, move-only bytes, charged to the budget that produced them.
class Buffer {
  public:
    Buffer() noexcept = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept
        : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)),
          budget_(std::exchange(other.budget_, nullptr)) {}
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            release();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
            budget_ = std::exchange(other.budget_, nullptr);
        }
        return *this;
    }
    ~Buffer() {
        release();
    }

    [[nodiscard]] std::span<std::byte> bytes() noexcept {
        return {data_, size_};
    }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {data_, size_};
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

  private:
    friend class Budget;
    Buffer(std::byte* data, std::size_t size, Budget* budget) noexcept
        : data_(data), size_(size), budget_(budget) {}
    void release() noexcept;
    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    Budget* budget_ = nullptr;
};

// The working-memory ceiling one run was given. A budget outlives every buffer it hands out, is
// neither copied nor moved, and counts atomically so that parallel work can share one ceiling.
class Budget {
  public:
    explicit Budget(std::size_t limit) noexcept : limit_(limit) {}
    Budget(const Budget&) = delete;
    Budget& operator=(const Budget&) = delete;
    Budget(Budget&&) = delete;
    Budget& operator=(Budget&&) = delete;
    ~Budget() = default;

    // Zero bytes is an empty buffer and costs nothing; anything else is charged before it is
    // allocated, and the charge is returned if the allocator cannot satisfy it.
    [[nodiscard]] Result<Buffer> allocate(std::size_t bytes);
    [[nodiscard]] std::size_t limit() const noexcept {
        return limit_;
    }
    [[nodiscard]] std::size_t used() const noexcept {
        return used_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t available() const noexcept {
        return limit_ - used();
    }

  private:
    friend class Buffer;
    [[nodiscard]] bool charge(std::size_t bytes) noexcept;
    void refund(std::size_t bytes) noexcept;
    std::size_t limit_;
    std::atomic<std::size_t> used_{0};
};
} // namespace docenhance::core
