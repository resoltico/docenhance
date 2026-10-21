// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "morphology_detail.hpp"
#include "morphology_working_detail.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
struct BackgroundRange {
    double minimum = 1;
    double maximum = 0;
};
bool range_row(Working working, image::PlaneView<const double> background, std::uint32_t y,
               BackgroundRange& partial) noexcept {
    const auto row = background.row(y);
    for (std::size_t x = 0; x < row.size(); ++x) {
        constexpr std::size_t interval = 1024;
        if (x % interval == 0 &&
            working.scheduler.get().cancellation().requested(core::Checkpoint::measurement)) {
            return false;
        }
        const auto value = row.subspan(x, 1).front();
        partial.minimum = std::min(partial.minimum, value);
        partial.maximum = std::max(partial.maximum, value);
    }
    return true;
}
core::Result<void> range(Working working, image::PlaneView<const double> background,
                         MorphologyAnalysis& analysis) {
    const auto bands = morphology_bands(background.height());
    std::array<BackgroundRange, morphology_bands_max> partials{};
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = morphology_lines(background.height(), band);
        BackgroundRange partial;
        for (auto y = first; y < last; ++y) {
            if (!range_row(working, background, y, partial)) {
                return core::cancelled();
            }
        }
        std::span{partials}.subspan(band, 1).front() = partial;
        return {};
    };
    if (auto result = working.scheduler.get().for_each(bands, exec::WorkRef{work}); !result) {
        return std::unexpected(std::move(result.error()));
    }
    analysis.background_min = 1;
    analysis.background_max = 0;
    for (const auto partial : std::span{partials}.first(bands)) {
        analysis.background_min = std::min(analysis.background_min, partial.minimum);
        analysis.background_max = std::max(analysis.background_max, partial.maximum);
    }
    return {};
}
} // namespace
core::Result<MorphologyAnalysis> measure_morphology(Working working,
                                                    image::PlaneView<const double> background,
                                                    const MorphologySourceAnalysis& source,
                                                    std::uint64_t eligible) {
    const auto capacity =
        static_cast<std::uint32_t>(std::min(eligible, std::uint64_t{illumination_sample_limit}));
    auto values = morphology_allocate<double>(working, capacity, 1);
    if (!values) {
        return std::unexpected(std::move(values.error()));
    }
    auto sampled = sample_morphology(working, background, values->view().row(0));
    if (!sampled) {
        return std::unexpected(std::move(sampled.error()));
    }
    if (sampled->count != source.sampling.count || sampled->stride != source.sampling.stride ||
        sampled->fallback != source.sampling.fallback) {
        return core::failure(core::ErrorCode::invariant,
                             "I02 eligibility changed during preparation");
    }
    const auto data = values->view().row(0).first(sampled->count);
    if (auto sorted = image::sort_samples(data, working.scheduler.get().cancellation()); !sorted) {
        return std::unexpected(std::move(sorted.error()));
    }
    const auto low = image::nearest_rank_index(data.size(), morphology_low_quantile);
    const auto middle = image::nearest_rank_index(data.size(), 0.5);
    const auto high = image::nearest_rank_index(data.size(), morphology_high_quantile);
    if (!low) {
        return std::unexpected(low.error());
    }
    if (!middle) {
        return std::unexpected(middle.error());
    }
    if (!high) {
        return std::unexpected(high.error());
    }
    MorphologyAnalysis analysis{
        .stride = sampled->stride,
        .count = sampled->count,
        .fallback = sampled->fallback,
        .analysis_fill = source.fill,
        .background_q10 = data.subspan(*low, 1).front(),
        .background_q50 = data.subspan(*middle, 1).front(),
        .background_q90 = data.subspan(*high, 1).front(),
    };
    if (auto reduced = range(working, background, analysis); !reduced) {
        return std::unexpected(std::move(reduced.error()));
    }
    return analysis;
}
} // namespace docenhance::methods
