// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/illumination.hpp"
#include "surface_detail.hpp"

#include <expected>
#include <span>
namespace docenhance::methods {
core::Result<SurfaceMeasurements>
surface_sample_measurements(std::span<double> y, std::span<double> b, SurfaceSampleCounts measured,
                            const core::Cancellation& cancellation) {
    constexpr double low_quantile = 0.1;
    constexpr double high_quantile = 0.9;
    auto sorted_b = image::sort_samples(b, cancellation);
    if (!sorted_b) {
        return std::unexpected(sorted_b.error());
    }
    auto sorted_y = image::sort_samples(y, cancellation);
    if (!sorted_y) {
        return std::unexpected(sorted_y.error());
    }
    const auto b10 = image::nearest_rank_index(b.size(), low_quantile).transform([b](auto i) {
        return b.subspan(i, 1).front();
    });
    const auto b50 = image::nearest_rank_index(b.size(), 0.5).transform([b](auto i) {
        return b.subspan(i, 1).front();
    });
    const auto b90 = image::nearest_rank_index(b.size(), high_quantile).transform([b](auto i) {
        return b.subspan(i, 1).front();
    });
    const auto y90 = image::nearest_rank_index(y.size(), high_quantile).transform([y](auto i) {
        return y.subspan(i, 1).front();
    });
    for (const auto* const selected : {&b10, &b50, &b90, &y90}) {
        if (!*selected) {
            return std::unexpected(selected->error());
        }
    }
    return SurfaceMeasurements{
        .stride = measured.stride,
        .count = measured.count,
        .fallback = measured.fallback,
        .target = *b90,
        .background_q10 = *b10,
        .background_q50 = *b50,
        .background_q90 = *b90,
        .luminance_q90 = *y90,
        .variation = surface_variation(*b10, *b90),
        .paper_fraction = static_cast<double>(measured.paper) / measured.count,
        .dark_fraction = static_cast<double>(measured.dark) / measured.count,
    };
}
} // namespace docenhance::methods
