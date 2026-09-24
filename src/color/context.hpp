// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <lcms2.h>
#include <memory>
#include <span>
#include <string>

namespace docenhance::color {
inline constexpr std::size_t native_block_count = 1024;
struct ColorMemory {
    std::reference_wrapper<core::Budget> budget;
    std::array<core::Buffer, native_block_count> blocks;
    bool exhausted = false;
    bool failed = false;
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
        return context_ != nullptr && !memory_.failed && !memory_.exhausted;
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
[[nodiscard]] core::Result<core::Buffer> output_profile(const Context& context,
                                                        core::Budget& budget, bool gray);
[[nodiscard]] Profile linear_rgb_profile(const Context& context);
[[nodiscard]] core::Result<Profile> source_profile(const Context& context,
                                                   const image::Raster& source);
[[nodiscard]] core::Result<void> validate_declarations(const image::PngMetadata& metadata);
} // namespace docenhance::color
