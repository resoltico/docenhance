// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/working.hpp"
#include "morphology_detail.hpp"
#include "morphology_working_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
core::Result<std::uint32_t> gather(Working working, image::PlaneView<const double> input,
                                   std::span<double> values, std::uint32_t stride) {
    std::uint32_t count = 0;
    std::size_t visited = 0;
    const auto cancellation = working.scheduler.get().cancellation();
    for (std::uint32_t y = 0; y < input.height() && count < values.size(); y += stride) {
        const auto row = input.row(y);
        const auto mask = working.protection.empty() ? std::span<const std::uint8_t>{}
                                                     : working.protection.row(y);
        for (std::uint32_t x = 0; x < input.width() && count < values.size(); x += stride) {
            constexpr std::size_t interval = 1024;
            if (visited++ % interval == 0 &&
                cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
                continue;
            }
            values.subspan(count++, 1).front() = row.subspan(x, 1).front();
        }
    }
    return count;
}
} // namespace
core::Result<MorphologySampling>
sample_morphology(Working working, image::PlaneView<const double> input, std::span<double> values) {
    MorphologySampling selection{
        .stride = morphology_lattice_stride({.width = input.width(), .height = input.height()}),
    };
    const auto work = [&](std::size_t) -> core::Result<void> {
        auto count = gather(working, input, values, selection.stride);
        if (!count) {
            return std::unexpected(std::move(count.error()));
        }
        selection.fallback = *count == 0;
        if (selection.fallback) {
            count = gather(working, input, values.first(1), 1);
            if (!count) {
                return std::unexpected(std::move(count.error()));
            }
        }
        selection.count = *count;
        return {};
    };
    if (auto sampled = working.scheduler.get().for_each(1, exec::WorkRef{work}); !sampled) {
        return std::unexpected(std::move(sampled.error()));
    }
    if (selection.count == 0) {
        return core::failure(core::ErrorCode::method_inapplicable,
                             "I02 has no eligible measurements");
    }
    return selection;
}
core::Result<MorphologySourceAnalysis> analyze_morphology(Working working, std::uint64_t eligible) {
    const auto capacity =
        static_cast<std::uint32_t>(std::min(eligible, std::uint64_t{illumination_sample_limit}));
    auto values = morphology_allocate<double>(working, capacity, 1);
    if (!values) {
        return std::unexpected(std::move(values.error()));
    }
    auto sampled = sample_morphology(working, working.luminance.as_const(), values->view().row(0));
    if (!sampled) {
        return std::unexpected(std::move(sampled.error()));
    }
    auto fill =
        image::nearest_rank(values->view().row(0).first(sampled->count), morphology_high_quantile,
                            working.scheduler.get().cancellation());
    if (!fill) {
        return std::unexpected(std::move(fill.error()));
    }
    return MorphologySourceAnalysis{.sampling = *sampled, .fill = *fill};
}
} // namespace docenhance::methods
