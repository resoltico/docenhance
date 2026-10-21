// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "illumination_gain.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace docenhance::methods {
inline constexpr std::size_t surface_bands_max = 64;
inline std::size_t surface_bands(std::uint32_t size) noexcept {
    return std::min(surface_bands_max, static_cast<std::size_t>(size));
}
inline std::pair<std::uint32_t, std::uint32_t> surface_band(std::uint32_t size,
                                                            std::size_t index) noexcept {
    const auto bands = surface_bands(size);
    return {static_cast<std::uint32_t>(std::uint64_t{index} * size / bands),
            static_cast<std::uint32_t>(std::uint64_t{index + 1} * size / bands)};
}
template <typename Sample>
core::Result<image::Plane<Sample>> surface_allocate(Working working, std::uint32_t width,
                                                    std::uint32_t height) {
    if (working.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    return image::Plane<Sample>::allocate(working.budget.get(), width, height);
}
struct SurfaceFit {
    SurfaceGrid grid;
    image::Plane<double> logarithms;
    SurfaceFacts facts;
};
struct SurfaceFieldView {
    SurfaceGrid grid;
    image::PlaneView<const double> logarithms;
};
struct SurfaceApplication {
    std::uint64_t evaluated{};
    std::uint64_t changed{};
    std::uint64_t capped{};
    std::uint64_t saturated{};
    double minimum = 1;
    double maximum = 1;
};
struct SurfaceMapInput {
    SurfaceFieldView field;
    IlluminationGain gain;
    bool target_valid = false;
};
[[nodiscard]] core::Result<SurfaceApplication>
map_surface_rows(Working working, SurfaceMapInput input, std::uint32_t first, std::uint32_t last);
// Actual producer boundaries; no pointwise Result scalar or success wrapper.
[[nodiscard]] core::Result<void> admit_surface_coefficients(Working working,
                                                            image::PlaneView<const double> logs);
[[nodiscard]] core::Result<SurfaceFit> fit_surface(const Surface& method, Working working,
                                                   SurfaceFacts facts, const SurfaceGrid& grid);
[[nodiscard]] core::Result<void> measure_surface_cells(Working working, const Surface& method,
                                                       const SurfaceGrid& grid,
                                                       image::PlaneView<double> work);
[[nodiscard]] core::Result<SurfaceMeasurements>
measure_surface_working(Working working, const SurfaceGrid& grid,
                        image::PlaneView<const double> logs, std::uint64_t eligible);
[[nodiscard]] core::Result<void> map_surface_working(Working working, const Surface& method,
                                                     const SurfaceGrid& grid,
                                                     image::PlaneView<const double> logs,
                                                     SurfaceFacts& facts);
} // namespace docenhance::methods
