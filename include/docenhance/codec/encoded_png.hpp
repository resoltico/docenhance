// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <span>
#include <utility>

namespace docenhance::codec {
// Capacity remains charged for the entire owner lifetime; only emitted bytes may be borrowed.
class EncodedPng {
  public:
    EncodedPng() noexcept = default;
    EncodedPng(const EncodedPng&) = delete;
    EncodedPng& operator=(const EncodedPng&) = delete;
    EncodedPng(EncodedPng&& other) noexcept
        : storage_(std::move(other.storage_)), used_(std::exchange(other.used_, 0)) {}
    EncodedPng& operator=(EncodedPng&& other) noexcept {
        if (this != &other) {
            storage_ = std::move(other.storage_);
            used_ = std::exchange(other.used_, 0);
        }
        return *this;
    }
    ~EncodedPng() = default;
    [[nodiscard]] static core::Result<EncodedPng> create(core::Buffer storage, std::size_t used) {
        if (used == 0 || used > storage.size() || storage.size() > core::bundle_max_file_bytes) {
            return core::failure(core::ErrorCode::argument, "Invalid encoded PNG ownership");
        }
        return EncodedPng{std::move(storage), used};
    }
    [[nodiscard]] bool ready() const noexcept {
        return used_ != 0;
    }
    [[nodiscard]] std::span<const std::byte> bytes() const& noexcept {
        return storage_.bytes().first(used_);
    }
    [[nodiscard]] std::span<const std::byte> bytes() && = delete;
    [[nodiscard]] std::span<const std::byte> bytes() const&& = delete;

  private:
    EncodedPng(core::Buffer storage, std::size_t used) noexcept
        : storage_(std::move(storage)), used_(used) {}
    core::Buffer storage_;
    std::size_t used_{};
};
} // namespace docenhance::codec
