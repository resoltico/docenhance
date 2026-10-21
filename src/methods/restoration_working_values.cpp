// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "restoration_working_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <variant>
namespace docenhance::methods {
RestorationBand restoration_band(std::uint32_t height, std::size_t index) noexcept {
    const auto first = static_cast<std::uint32_t>(index * restoration_band_rows);
    return {.first = first, .rows = std::min(restoration_band_rows, height - first)};
}
core::Result<RestorationGeometry> restoration_geometry(Working working, const ResolvedPsf& psf) {
    const auto guard =
        std::max(restoration_minimum_guard,
                 restoration_guard_radius_factor * std::max(psf.width / 2, psf.height / 2));
    auto width =
        restoration_fft_size(std::uint64_t{working.luminance.width()} + (std::uint64_t{2} * guard));
    auto height = restoration_fft_size(std::uint64_t{working.luminance.height()} +
                                       (std::uint64_t{2} * guard));
    if (!width) {
        return std::unexpected(std::move(width.error()));
    }
    if (!height) {
        return std::unexpected(std::move(height.error()));
    }
    return RestorationGeometry{.guard = guard, .width = *width, .height = *height};
}
core::Result<PsfFacts> restoration_psf_facts(const Wiener& method, const ResolvedPsf& psf) {
    if (auto valid = validate_psf(psf); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    if (!canonical_psf(method.parameters(), psf)) {
        return core::failure(core::ErrorCode::argument,
                             "Resolved PSF does not match requested model");
    }
    const auto centroid = psf_centroid(psf.width, psf.height, psf.coefficients);
    if (!centroid) {
        return std::unexpected(centroid.error());
    }
    double sum = 0;
    for (const auto coefficient : psf.coefficients) {
        sum += coefficient;
    }
    const auto& selection = method.parameters().psf;
    auto kind = PsfKind::kernel;
    if (std::holds_alternative<GaussianPsf>(selection)) {
        kind = PsfKind::gaussian;
    } else if (std::holds_alternative<MotionPsf>(selection)) {
        kind = PsfKind::motion;
    }
    return PsfFacts{
        .kind = kind,
        .width = psf.width,
        .height = psf.height,
        .normalized_sum = sum,
        .centroid_x = centroid->front(),
        .centroid_y = centroid->back(),
    };
}
} // namespace docenhance::methods
