// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <span>
#include <utility>

namespace docenhance::core {
// A page of a scanned document is large: 40 megapixels of 32-bit samples is about 153 MiB per
// plane, and a pipeline holds several at once. Two consequences shape every allocation in this
// project.
//
// First, running out of memory is an expected outcome of a legitimate request, not a defect, so it
// is a value: image/codec payload buffers are obtained through the charged Budget owner
// using Result. Small metadata and OS resources
// are outside the byte budget and are translated to resource failures at execution boundaries.
//
// Second, memory that is not accounted for cannot be limited. The host supplies a working
// allocation budget, so every image buffer is charged against one ledger and refunded when the
// buffer dies. A buffer owns its memory alone: it cannot be copied, only moved, so no operation
// silently duplicates a page. The ledger itself is fixed-size control metadata.

// Rows begin on this boundary so that vector loads on any supported target are aligned.
inline constexpr std::size_t buffer_alignment = 64;

class Budget;
struct BudgetState;
// A ledger-only lease for bounded native-owned working storage. It allocates no dummy payload
// and retains the same shared ledger lifetime/refund rules as Buffer.
class Reservation {
  public:
    Reservation() noexcept = default;
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
    Reservation(Reservation&& other) noexcept
        : size_(std::exchange(other.size_, 0)), state_(std::exchange(other.state_, nullptr)) {}
    Reservation& operator=(Reservation&& other) noexcept;
    ~Reservation();
    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

  private:
    friend class Budget;
    Reservation(std::size_t size, BudgetState* state) noexcept : size_(size), state_(state) {}
    void release() noexcept;
    std::size_t size_ = 0;
    BudgetState* state_ = nullptr;
};

// Owning, aligned, move-only bytes, charged to the budget that produced them.
class Buffer {
  public:
    Buffer() noexcept = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept
        : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)),
          state_(std::exchange(other.state_, nullptr)) {}
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            release();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
            state_ = std::exchange(other.state_, nullptr);
        }
        return *this;
    }
    ~Buffer() {
        release();
    }

    // Borrow only from a live lvalue owner; moving or destroying it invalidates the span.
    [[nodiscard]] std::span<std::byte> bytes() & noexcept {
        return {data_, size_};
    }
    [[nodiscard]] std::span<const std::byte> bytes() const& noexcept {
        return {data_, size_};
    }
    [[nodiscard]] std::span<std::byte> bytes() && = delete;
    [[nodiscard]] std::span<const std::byte> bytes() const&& = delete;
    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

  private:
    friend class Budget;
    Buffer(std::byte* data, std::size_t size, BudgetState* state) noexcept
        : data_(data), size_(size), state_(state) {}
    void release() noexcept;
    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    BudgetState* state_ = nullptr;
};

// The working-memory ceiling one run was given. Its shared ledger outlives both the budget and
// every buffer it hands out. Atomic accounting lets parallel work share one ceiling safely.
class Budget {
  public:
    explicit Budget(std::size_t limit) noexcept;
    Budget(const Budget&) = delete;
    Budget& operator=(const Budget&) = delete;
    Budget(Budget&&) = delete;
    Budget& operator=(Budget&&) = delete;
    ~Budget();

    // Zero bytes is an empty buffer and costs nothing; anything else is charged before it is
    // allocated, and the charge is returned if the allocator cannot satisfy it.
    [[nodiscard]] Result<Buffer> allocate(std::size_t bytes);
    // Used includes both directly owned buffers and live native reservations.
    [[nodiscard]] Result<Reservation> reserve(std::size_t bytes);
    [[nodiscard]] std::size_t limit() const noexcept {
        return limit_;
    }
    [[nodiscard]] std::size_t used() const noexcept;
    [[nodiscard]] std::size_t available() const noexcept {
        return state_ == nullptr ? 0 : limit_ - used();
    }

  private:
    friend class Buffer;
    [[nodiscard]] bool charge(std::size_t bytes) noexcept;
    std::size_t limit_;
    BudgetState* state_;
};
} // namespace docenhance::core
