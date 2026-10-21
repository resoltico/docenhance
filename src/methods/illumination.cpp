// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/illumination.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/parameters.hpp"
#include "morphology_detail.hpp"

#include <algorithm>
#include <cstdint>

namespace docenhance::methods {
core::Result<Morphology> Morphology::create(MorphologyParameters p) {
    if (!valid_parameters(p)) {
        return core::failure(core::ErrorCode::argument, "Invalid I02 morphological parameters");
    }
    return Morphology{p};
}
std::uint32_t morphology_radius(image::Extent extent, const Morphology& method) noexcept {
    const auto parameters = method.parameters();
    if (parameters.radius) {
        return *parameters.radius;
    }
    constexpr std::uint32_t scale = 50;
    const auto minimum = std::min(extent.width, extent.height);
    const auto rounded =
        (minimum / scale) + static_cast<std::uint32_t>(minimum % scale >= scale / 2);
    return std::clamp(rounded, morphology_automatic_min, morphology_automatic_max);
}
core::Result<Surface> Surface::create(SurfaceParameters parameters) {
    if (!valid_parameters(parameters)) {
        return core::failure(core::ErrorCode::argument, "Invalid I01 surface parameters");
    }
    return Surface{parameters};
}
} // namespace docenhance::methods
