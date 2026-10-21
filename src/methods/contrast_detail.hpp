// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/working.hpp"

#include <cstdint>
#include <span>
namespace docenhance::methods {
inline constexpr std::uint32_t clahe_statistic_channels = 3;
[[nodiscard]] std::uint32_t tile_index(std::uint32_t coordinate, std::uint32_t size,
                                       std::uint32_t divisions);
[[nodiscard]] core::Result<bool> make_knots(std::span<double> knots,
                                            std::span<const double> statistics, double clip);
[[nodiscard]] core::Result<ClaheMaps> fit_clahe(const Clahe& method, Working working,
                                                std::uint64_t eligible);
// Finite unit f, admitted position/extent/method and its complete fitted maps.
[[nodiscard]] double clahe_candidate(double f, image::Coordinate position, image::Extent extent,
                                     const Clahe& method, const ClaheMaps& maps) noexcept;
} // namespace docenhance::methods
