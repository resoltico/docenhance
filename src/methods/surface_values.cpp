// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/range.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/illumination.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace docenhance::methods {
bool validate(const SolverReport& solution) noexcept {
    return solution.iterations <= surface_iteration_limit && std::isfinite(solution.tolerance) &&
           solution.tolerance > 0 && core::in_range(solution.residual, 0.0, solution.tolerance);
}
std::optional<std::uint32_t> surface_fraction_count(double fraction,
                                                    std::uint32_t samples) noexcept {
    if (!core::in_range(fraction, 0.0, 1.0) || samples == 0 || samples > surface_sample_limit) {
        return std::nullopt;
    }
    const auto count = static_cast<std::uint32_t>(std::round(fraction * samples));
    return fraction == static_cast<double>(count) / samples ? std::optional{count} : std::nullopt;
}
namespace {
bool joint_fraction_counts(const SurfaceMeasurements& measurements, std::uint32_t paper,
                           std::uint32_t dark) noexcept {
    const auto high = image::nearest_rank_index(measurements.count, 0.9);
    if (!high) {
        return false;
    }
    const auto h = static_cast<std::uint32_t>(*high);
    if (measurements.luminance_q90 == 0 && (dark < h + 1 || paper > measurements.count - h - 1)) {
        return false;
    }
    if ((surface_paper_sample(measurements.luminance_q90, measurements.background_q90) &&
         paper == 0) ||
        (!surface_dark_sample(measurements.luminance_q90, measurements.background_q90) &&
         dark == measurements.count)) {
        return false;
    }
    const std::array quantiles{
        std::pair{0.1, measurements.background_q10},
        std::pair{0.5, measurements.background_q50},
        std::pair{0.9, measurements.background_q90},
    };
    // The rank intersections overlap; each is a separate bound, never a sum.
    return std::ranges::all_of(quantiles, [&](const auto& point) noexcept {
        const auto& [fraction, background] = point;
        const auto selected = image::nearest_rank_index(measurements.count, fraction);
        if (!selected || *selected > h) {
            return false;
        }
        const auto intersection = h + 1 - static_cast<std::uint32_t>(*selected);
        return (!surface_dark_sample(measurements.luminance_q90, background) ||
                dark >= intersection) &&
               (surface_paper_sample(measurements.luminance_q90, background) ||
                paper <= measurements.count - intersection);
    });
}
} // namespace
bool validate(const SurfaceMeasurements& measurements) noexcept {
    constexpr auto maximum_stride =
        (image::source_pixels_max + surface_lattice_extent - 1) / surface_lattice_extent;
    if (measurements.count == 0 || measurements.count > surface_sample_limit ||
        measurements.stride == 0 || measurements.stride > maximum_stride ||
        (measurements.fallback && measurements.stride == 1) ||
        !core::in_range(measurements.target, 0.0, 1.0) || measurements.target == 0 ||
        !core::in_range(measurements.background_q10, 0.0, 1.0) ||
        measurements.background_q10 == 0 ||
        !core::in_range(measurements.background_q50, measurements.background_q10, 1.0) ||
        !core::in_range(measurements.background_q90, measurements.background_q50, 1.0) ||
        !core::in_range(measurements.luminance_q90, 0.0, 1.0) ||
        measurements.variation !=
            surface_variation(measurements.background_q10, measurements.background_q90)) {
        return false;
    }
    const auto paper = surface_fraction_count(measurements.paper_fraction, measurements.count);
    const auto dark = surface_fraction_count(measurements.dark_fraction, measurements.count);
    const auto low = image::nearest_rank_index(measurements.count, 0.1);
    const auto middle = image::nearest_rank_index(measurements.count, 0.5);
    const auto high = image::nearest_rank_index(measurements.count, 0.9);
    return paper && dark && *paper + *dark <= measurements.count && low && middle && high &&
           (*low != *middle || measurements.background_q10 == measurements.background_q50) &&
           (*middle != *high || measurements.background_q50 == measurements.background_q90) &&
           joint_fraction_counts(measurements, *paper, *dark);
}
double surface_coverage(std::uint32_t measured, std::uint32_t cells) noexcept {
    return static_cast<double>(measured) / cells;
}
double surface_variation(double low, double high) noexcept {
    return (high - low) / std::max(high, illumination_floor);
}
std::uint32_t surface_lattice_stride(image::Extent extent) noexcept {
    const auto maximum = std::max(extent.width, extent.height);
    return std::max(std::uint32_t{1},
                    (maximum / surface_lattice_extent) +
                        static_cast<std::uint32_t>(maximum % surface_lattice_extent != 0));
}
SurfacePredicates surface_decisions(const SurfaceMeasurements& measurements,
                                    double coverage) noexcept {
    constexpr double minimum_brightness = 0.35;
    constexpr double minimum_background = 0.20;
    constexpr double minimum_variation = 0.08;
    constexpr double minimum_paper = 0.55;
    constexpr double minimum_dark = 0.001;
    constexpr double maximum_dark = 0.40;
    return {
        .coverage = coverage >= surface_automatic_coverage,
        .bright_quantile = measurements.luminance_q90 >= minimum_brightness,
        .median_background = measurements.background_q50 >= minimum_background,
        .background_variation = measurements.variation >= minimum_variation,
        .paper_fraction = measurements.paper_fraction >= minimum_paper,
        .dark_fraction = measurements.dark_fraction >= minimum_dark &&
                         measurements.dark_fraction <= maximum_dark,
    };
}
} // namespace docenhance::methods
