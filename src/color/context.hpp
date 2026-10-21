// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <lcms2.h>
#include <memory>
#include <span>
#include <string>

namespace docenhance::color {
inline constexpr std::size_t native_block_count = 1024;
// No lock escapes a callback, and nested realloc uses only already-locked helpers.
// A concrete metadata lock never throws, allocates, or stays held across native transformation.
class MetadataLock {
  public:
    explicit MetadataLock(std::atomic_flag& flag) noexcept : flag_(flag) {
        while (flag_.get().test_and_set(std::memory_order_acquire)) {
        }
    }
    MetadataLock(const MetadataLock&) = delete;
    MetadataLock& operator=(const MetadataLock&) = delete;
    MetadataLock(MetadataLock&&) = delete;
    MetadataLock& operator=(MetadataLock&&) = delete;
    ~MetadataLock() {
        flag_.get().clear(std::memory_order_release);
    }

  private:
    std::reference_wrapper<std::atomic_flag> flag_;
};
struct ColorMemory {
    std::reference_wrapper<core::Budget> budget;
    std::array<core::Buffer, native_block_count> blocks;
    std::atomic_flag guard;
    std::atomic<bool> exhausted{false};
    std::atomic<bool> failed{false};
};
class Context {
  public:
    explicit Context(core::Budget& budget);
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;
    ~Context();
    [[nodiscard]] cmsContext get() const noexcept {
        return context_;
    }
    [[nodiscard]] bool good() const noexcept {
        return context_ != nullptr && !memory_.failed.load(std::memory_order_relaxed) &&
               !memory_.exhausted.load(std::memory_order_relaxed);
    }
    [[nodiscard]] core::Error error(std::string message) const;

  private:
    ColorMemory memory_;
    cmsContext context_ = nullptr;
};
struct ProfileCloser {
    void operator()(void* profile) const noexcept;
};
struct CurveCloser {
    void operator()(cmsToneCurve* curve) const noexcept;
};
struct TransformCloser {
    void operator()(void* transform) const noexcept;
};
using Profile = std::unique_ptr<void, ProfileCloser>;
using Curve = std::unique_ptr<cmsToneCurve, CurveCloser>;
using Transform = std::unique_ptr<void, TransformCloser>;
[[nodiscard]] core::Result<core::Buffer>
output_profile(const Context& context, core::Budget& budget, bool gray,
               const core::Cancellation& cancellation = {});
[[nodiscard]] Curve srgb_transfer(const Context& context);
[[nodiscard]] Profile linear_rgb_profile(const Context& context);
[[nodiscard]] Profile linear_gray_profile(const Context& context);
[[nodiscard]] core::Result<Profile> source_profile(const Context& context,
                                                   const image::Raster& source);
[[nodiscard]] core::Result<void> validate_declarations(const image::RasterMetadata& metadata);
} // namespace docenhance::color
