// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"

#include <algorithm>
#include <expected>
#include <functional>
#include <span>
#include <utility>
namespace docenhance::methods {
inline constexpr std::uint32_t illumination_sample_limit = 1048576;
inline constexpr std::uint32_t morphology_lattice_extent = 1024;
inline constexpr std::uint32_t morphology_automatic_min = 8;
inline constexpr std::uint32_t morphology_automatic_max = 128;
struct MorphologyContext {
    MorphologyContext(IlluminationInput input_value, core::Budget& budget_value,
                      const core::Cancellation& cancellation_value,
                      IlluminationReport& report_value)
        : input(input_value), budget(budget_value), cancellation(cancellation_value),
          report(report_value) {}
    IlluminationInput input;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
    std::reference_wrapper<IlluminationReport> report;
};
template <typename Sample>
[[nodiscard]] core::Result<image::Plane<Sample>>
morphology_plane(MorphologyContext context, std::uint32_t width, std::uint32_t height = 1) {
    if (context.cancellation.get().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto result = image::Plane<Sample>::allocate(context.budget.get(), width, height);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (!context.report.get().morphology) {
        return core::failure(core::ErrorCode::argument, "Missing I02 preparation observations");
    }
    auto& detail = context.report.get().morphology.value();
    detail.preparation_charge_peak = std::max(
        detail.preparation_charge_peak, static_cast<std::uint64_t>(context.budget.get().used()));
    return std::move(*result);
}
struct MorphologySampling {
    std::uint32_t stride{};
    std::uint32_t count{};
    bool fallback = false;
};
[[nodiscard]] core::Result<void> morphology_counts(MorphologyContext context);
[[nodiscard]] core::Result<MorphologySampling>
morphology_samples(MorphologyContext context, std::span<double> values, std::span<double> rgb,
                   image::PlaneView<const double> background = {});
[[nodiscard]] core::Result<void> morphology_fill(MorphologyContext context,
                                                 image::PlaneView<double> output,
                                                 std::span<double> rgb, double fill);
[[nodiscard]] core::Result<void>
morphology_background_statistics(MorphologyContext context,
                                 image::PlaneView<const double> background);
} // namespace docenhance::methods
