// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "morphology_detail.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
namespace docenhance::methods {
core::Result<void> morphology_background_statistics(MorphologyContext context,
                                                    image::PlaneView<const double> background) {
    const auto extent = context.input.source.get().extent();
    const auto capacity = static_cast<std::uint32_t>(std::min(
        std::uint64_t{extent.width} * extent.height, std::uint64_t{illumination_sample_limit}));
    auto values = morphology_plane<double>(context, capacity);
    if (!values) {
        return std::unexpected(values.error());
    }
    auto sampled = morphology_samples(context, values->view().row(0), {}, background);
    if (!sampled) {
        return std::unexpected(sampled.error());
    }
    if (!context.report.get().morphology) {
        return core::failure(core::ErrorCode::argument, "Missing I02 preparation observations");
    }
    auto& detail = context.report.get().morphology.value();
    if (detail.count != sampled->count || detail.stride != sampled->stride ||
        detail.fallback != sampled->fallback) {
        return core::failure(core::ErrorCode::invariant,
                             "I02 eligibility changed during preparation");
    }
    const auto data = values->view().row(0).first(sampled->count);
    constexpr double low_quantile = 0.1;
    constexpr double high_quantile = 0.9;
    auto sorted = image::sort_samples(data, context.cancellation.get());
    if (!sorted) {
        return std::unexpected(sorted.error());
    }
    const auto low = image::nearest_rank_index(data.size(), low_quantile);
    const auto middle = image::nearest_rank_index(data.size(), 0.5);
    const auto high = image::nearest_rank_index(data.size(), high_quantile);
    if (!low) {
        return std::unexpected(low.error());
    }
    if (!middle) {
        return std::unexpected(middle.error());
    }
    if (!high) {
        return std::unexpected(high.error());
    }
    detail.background_q10 = data.subspan(*low, 1).front();
    detail.background_q50 = data.subspan(*middle, 1).front();
    detail.background_q90 = data.subspan(*high, 1).front();
    detail.background_min = 1;
    detail.background_max = 0;
    constexpr std::uint32_t interval = 1024;
    for (std::uint32_t y = 0; y < background.height(); ++y) {
        const auto row = background.row(y);
        for (std::uint32_t x = 0; x < background.width(); ++x) {
            if (x % interval == 0 &&
                context.cancellation.get().requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            const auto value = row.subspan(x, 1).front();
            detail.background_min = std::min(detail.background_min, value);
            detail.background_max = std::max(detail.background_max, value);
        }
    }
    return {};
}
} // namespace docenhance::methods
