// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"

#include <cstdint>
#include <span>
#include <utility>
namespace docenhance::methods {
struct ExtremaPass {
    std::uint32_t radius{};
    bool horizontal = true;
    bool dilation = true;
};
struct GaussianPass {
    std::span<const double> weights;
    double normalization{};
    bool horizontal = true;
};
[[nodiscard]] core::Result<void> extrema_pass(image::PlaneView<const double> input,
                                              image::PlaneView<double> output,
                                              std::span<std::uint64_t> queue, ExtremaPass pass,
                                              const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<void> gaussian_pass(image::PlaneView<const double> input,
                                               image::PlaneView<double> output, GaussianPass pass,
                                               const core::Cancellation& cancellation = {});
[[nodiscard]] std::uint32_t morphology_radius(image::Extent extent,
                                              const Morphology& method) noexcept;
// Checks method-specific partial fields; shared counters and extents have separate checks.
[[nodiscard]] bool valid_morphology_observations(const IlluminationReport& report);
class MorphologyModel {
  public:
    [[nodiscard]] static core::Result<MorphologyModel>
    prepare(IlluminationInput input, const Morphology& method, core::Budget& budget,
            const core::Cancellation& cancellation, IlluminationReport& report);
    ~MorphologyModel() = default;
    MorphologyModel(const MorphologyModel&) = delete;
    MorphologyModel& operator=(const MorphologyModel&) = delete;
    MorphologyModel(MorphologyModel&& other) noexcept
        : extent_(std::exchange(other.extent_, {})), field_(std::move(other.field_)),
          method_(other.method_), target_(std::exchange(other.target_, 0)),
          active_(std::exchange(other.active_, false)) {}
    MorphologyModel& operator=(MorphologyModel&& other) noexcept {
        if (this != &other) {
            extent_ = std::exchange(other.extent_, {});
            field_ = std::move(other.field_);
            method_ = other.method_;
            target_ = std::exchange(other.target_, 0);
            active_ = std::exchange(other.active_, false);
        }
        return *this;
    }
    [[nodiscard]] bool active() const noexcept {
        return active_;
    }
    [[nodiscard]] core::Result<double> background(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] core::Result<void> apply(image::RowRange range, std::span<double> rgb,
                                           image::PlaneView<const std::uint8_t> protection,
                                           IlluminationReport& report,
                                           const core::Cancellation& cancellation) const;

  private:
    MorphologyModel(image::Extent extent, image::Plane<double> field, Morphology method,
                    double target, bool active)
        : extent_(extent), field_(std::move(field)), method_(method), target_(target),
          active_(active) {}
    image::Extent extent_;
    image::Plane<double> field_;
    Morphology method_;
    double target_{};
    bool active_ = false;
};
} // namespace docenhance::methods
