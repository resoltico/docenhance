// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/illumination.hpp"

#include <cstdint>

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
} // namespace docenhance::methods
