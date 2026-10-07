// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/contrast.hpp"
namespace docenhance::methods {
inline constexpr std::uint32_t clahe_statistic_channels = 3;
[[nodiscard]] core::Result<ClaheMaps> measure_clahe(image::LinearSource& source,
                                                    image::PlaneView<const std::uint8_t> mask,
                                                    const Clahe& method,
                                                    const ContrastExecution& execution);
[[nodiscard]] double clahe_candidate(double f, image::RowRange position, image::Extent extent,
                                     const Clahe& method, const ClaheMaps& maps);
[[nodiscard]] core::Result<LevelsRange> measure_levels(image::LinearSource& source,
                                                       image::PlaneView<const std::uint8_t> mask,
                                                       const Levels& method,
                                                       const ContrastExecution& execution);
} // namespace docenhance::methods
