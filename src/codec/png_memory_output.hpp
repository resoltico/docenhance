// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/codec/encoded_png.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

namespace docenhance::codec {
// The native writer borrows this owner. Callback failures finish their C++ unwind before the
// caller raises a native error; no Result or Buffer is live in a frame crossed by longjmp.
class PngMemoryOutput {
  public:
    explicit PngMemoryOutput(core::Budget& budget) noexcept : budget_(budget) {}
    [[nodiscard]] PngOutput stream() noexcept;
    [[nodiscard]] bool append(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] bool exhausted() const noexcept {
        return exhausted_;
    }
    [[nodiscard]] core::Result<EncodedPng> finish() && {
        if (exhausted_) {
            return core::failure(core::ErrorCode::resource, "The PNG memory writer failed");
        }
        return EncodedPng::create(std::move(storage_), std::exchange(used_, 0));
    }

  private:
    [[nodiscard]] bool grow(std::size_t required);
    std::reference_wrapper<core::Budget> budget_;
    core::Buffer storage_;
    std::size_t used_{};
    bool exhausted_ = false;
};
[[nodiscard]] core::Result<void> write_png(PngOutput output, PngImage image, core::Budget& budget,
                                           const core::Cancellation& cancellation);
} // namespace docenhance::codec
