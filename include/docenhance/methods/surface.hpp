// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

namespace docenhance::methods {
struct SurfaceGrid {
    image::Extent extent;
    std::uint32_t cell{};
    std::uint32_t columns{};
    std::uint32_t rows{};
};
// Resolve the admitted grid without fitting or inspecting source samples.
[[nodiscard]] core::Result<SurfaceGrid> surface_grid(image::Extent extent, const Surface& method);
inline constexpr std::uint32_t surface_cell_limit = 65536;
inline constexpr std::uint32_t surface_sample_limit = 1048576;
inline constexpr double surface_floor = 0.02;
// Mask is already in oriented coordinates; an empty view means no protected samples.
struct SurfaceInput {
    std::reference_wrapper<image::LinearSource> source;
    image::PlaneView<const std::uint8_t> protection;

    SurfaceInput(image::LinearSource& linear, image::PlaneView<const std::uint8_t> mask) noexcept
        : source(linear), protection(mask) {}
};
class SurfaceModel {
  public:
    [[nodiscard]] static core::Result<SurfaceModel>
    prepare(SurfaceInput input, const Surface& method, core::Budget& budget,
            const core::Cancellation& cancellation, IlluminationReport& report);
    SurfaceModel(const SurfaceModel&) = delete;
    SurfaceModel& operator=(const SurfaceModel&) = delete;
    SurfaceModel(SurfaceModel&& other) noexcept
        : grid_(std::exchange(other.grid_, {})), method_(other.method_),
          logarithms_(std::move(other.logarithms_)), target_(std::exchange(other.target_, 0)),
          active_(std::exchange(other.active_, false)) {}
    SurfaceModel& operator=(SurfaceModel&& other) noexcept {
        if (this != &other) {
            grid_ = std::exchange(other.grid_, {});
            method_ = other.method_;
            logarithms_ = std::move(other.logarithms_);
            target_ = std::exchange(other.target_, 0);
            active_ = std::exchange(other.active_, false);
        }
        return *this;
    }
    ~SurfaceModel() = default;
    [[nodiscard]] bool active() const noexcept {
        return active_;
    }
    [[nodiscard]] SurfaceGrid grid() const noexcept {
        return grid_;
    }
    [[nodiscard]] core::Result<double> background(std::uint32_t x, std::uint32_t y) const;
    // Mutates only unprotected RGB triplets; statistics count output traversal only.
    [[nodiscard]] core::Result<void> apply(image::RowRange range, std::span<double> rgb,
                                           image::PlaneView<const std::uint8_t> protection,
                                           IlluminationReport& report,
                                           const core::Cancellation& cancellation) const;

  private:
    SurfaceModel(SurfaceGrid grid, Surface method, image::Plane<double> logarithms, double target,
                 bool active)
        : grid_(grid), method_(method), logarithms_(std::move(logarithms)), target_(target),
          active_(active) {}
    SurfaceGrid grid_;
    Surface method_;
    image::Plane<double> logarithms_;
    double target_{};
    bool active_ = false;
};
} // namespace docenhance::methods
