// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "morphology_reference.hpp"
#include "morphology_working_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>
#include <vector>
namespace docenhance::tests {
struct MorphologyWorkingReference {
    std::vector<double> samples;
    methods::MorphologyFacts facts;
};
inline double morphology_reference_rank(std::vector<double> samples, double p) {
    require(!samples.empty(), "independent selected population is nonempty");
    std::ranges::sort(samples);
    const auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(samples.size())));
    return samples.at(rank == 0 ? 0 : rank - 1);
}
inline std::vector<std::size_t> morphology_selected_points(MorphologyWorkingFixture& fixture,
                                                           std::uint32_t stride) {
    std::vector<std::size_t> selected;
    for (std::uint32_t y = 0; y < fixture.luminance.height(); y += stride) {
        const auto mask = fixture.protection.view().row(y);
        for (std::uint32_t x = 0; x < fixture.luminance.width(); x += stride) {
            if (mask.subspan(x, 1).front() == 0) {
                selected.push_back((std::size_t{y} * fixture.luminance.width()) + x);
            }
        }
    }
    return selected;
}
inline bool morphology_constant_window(std::span<const double> input,
                                       MorphologyReferenceExtent extent,
                                       MorphologyReferencePoint point, unsigned radius) {
    const auto center = input[(std::size_t{point.y} * extent.width) + point.x];
    const auto support = static_cast<std::int64_t>(std::ceil(1.5 * radius));
    for (auto dy = -support; dy <= support; ++dy) {
        for (auto dx = -support; dx <= support; ++dx) {
            const auto x = morphology_reflect(std::int64_t{point.x} + dx, extent.width);
            const auto y = morphology_reflect(std::int64_t{point.y} + dy, extent.height);
            if (input[(std::size_t{y} * extent.width) + x] != center) {
                return false;
            }
        }
    }
    return true;
}
// A separate dense, ordered specification model gives exact threshold/counter decisions;
// the independent two-dimensional Gaussian below still challenges its numerical field.
inline double morphology_ordered_convolution(std::span<const double> input,
                                             MorphologyReferenceExtent extent,
                                             MorphologyReferencePoint point,
                                             std::span<const double> weights, bool horizontal) {
    std::vector<double> samples;
    samples.reserve(weights.size());
    const auto center = input[(std::size_t{point.y} * extent.width) + point.x];
    for (std::size_t k = 0; k < weights.size(); ++k) {
        const auto offset =
            static_cast<std::int64_t>(k) - static_cast<std::int64_t>(weights.size() / 2);
        const auto x =
            horizontal ? morphology_reflect(std::int64_t{point.x} + offset, extent.width) : point.x;
        const auto y = horizontal
                           ? point.y
                           : morphology_reflect(std::int64_t{point.y} + offset, extent.height);
        samples.push_back(input[(std::size_t{y} * extent.width) + x]);
    }
    if (std::ranges::all_of(samples, [&](double value) { return value == center; })) {
        return center;
    }
    const auto denominator = std::accumulate(weights.begin(), weights.end(), 0.0);
    return std::inner_product(samples.begin(), samples.end(), weights.begin(), 0.0) / denominator;
}
inline std::vector<double> morphology_ordered_axis(std::span<const double> input,
                                                   MorphologyReferenceExtent extent,
                                                   std::span<const double> weights,
                                                   bool horizontal) {
    std::vector<double> result(input.size());
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            result.at((std::size_t{y} * extent.width) + x) = morphology_ordered_convolution(
                input, extent, {.x = x, .y = y}, weights, horizontal);
        }
    }
    return result;
}
inline std::vector<double> morphology_ordered_gaussian(std::span<const double> input,
                                                       MorphologyReferenceExtent extent,
                                                       unsigned radius) {
    const double sigma = static_cast<double>(radius) / 2;
    const auto support = static_cast<unsigned>(std::ceil(3 * sigma));
    std::vector<double> weights((2 * support) + 1);
    for (unsigned i = 0; i < weights.size(); ++i) {
        const auto offset = static_cast<double>(i) - support;
        weights.at(i) = std::exp(-(offset * offset) / (2 * sigma * sigma));
    }
    const auto horizontal = morphology_ordered_axis(input, extent, weights, true);
    return morphology_ordered_axis(horizontal, extent, weights, false);
}
inline std::vector<double> morphology_reference_field(std::span<const double> entering,
                                                      MorphologyReferenceExtent extent,
                                                      unsigned radius) {
    const auto dilation = square_reference(entering, extent, radius, true);
    auto closing = square_reference(dilation, extent, radius, false);
    if (std::ranges::all_of(closing, [&](double value) { return value == closing.front(); })) {
        return closing;
    }
    const auto ordered = morphology_ordered_gaussian(closing, extent, radius);
    std::vector<double> result(closing.size());
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const auto index = (std::size_t{y} * extent.width) + x;
            const MorphologyReferencePoint point{.x = x, .y = y};
            result.at(index) = morphology_constant_window(closing, extent, point, radius)
                                   ? closing.at(index)
                                   : gaussian_sample(closing, extent, point, radius);
            require(
                std::abs(ordered.at(index) - result.at(index)) < 1e-12,
                "ordered decision model agrees with independent true2D Gaussian at existing bound");
        }
    }
    return ordered;
}
inline void morphology_reference_map(MorphologyWorkingFixture& fixture,
                                     std::span<const double> background,
                                     methods::MorphologyParameters parameters,
                                     MorphologyWorkingReference& reference) {
    if (!reference.facts.analysis) {
        require(false, "independent map owns actual completed background analysis");
        return;
    }
    const auto target = reference.facts.analysis->target;
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        const auto source = fixture.luminance.view().row(y);
        const auto mask = fixture.protection.view().row(y);
        for (std::uint32_t x = 0; x < source.size(); ++x) {
            if (mask.subspan(x, 1).front() != 0) {
                continue;
            }
            const auto b = background[(std::size_t{y} * source.size()) + x];
            const auto ratio = target / std::max(b, 0.02);
            const auto gain = std::clamp(ratio, 1.0, parameters.max_gain);
            const auto raw = source.subspan(x, 1).front() * std::pow(gain, parameters.strength);
            auto& facts = reference.facts;
            if (facts.counts.evaluated == 0) {
                facts.min_gain = gain;
                facts.max_gain = gain;
            } else {
                facts.min_gain = std::min(facts.min_gain, gain);
                facts.max_gain = std::max(facts.max_gain, gain);
            }
            ++facts.counts.evaluated;
            facts.gain_capped_samples += static_cast<std::uint64_t>(ratio >= parameters.max_gain);
            facts.saturated_samples += static_cast<std::uint64_t>(raw > 1);
            const auto next = std::clamp(raw, 0.0, 1.0);
            if (next != source.subspan(x, 1).front()) {
                reference.samples.at((std::size_t{y} * fixture.luminance.view().row_pitch()) + x) =
                    next;
                ++facts.counts.changed;
            }
        }
    }
    reference.facts.disposition = reference.facts.counts.changed == 0
                                      ? methods::Disposition::no_change
                                      : methods::Disposition::applied;
    reference.facts.reason = reference.facts.counts.changed == 0
                                 ? methods::MorphologyReason::no_effect
                                 : methods::MorphologyReason::none;
}
inline std::vector<double> morphology_reference_input(MorphologyWorkingFixture& fixture,
                                                      MorphologyWorkingReference& result) {
    const auto view = fixture.luminance.view();
    result.samples.assign(view.storage().begin(), view.storage().end());
    std::vector<double> entering;
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        const auto mask = fixture.protection.view().row(y);
        for (std::uint32_t x = 0; x < view.width(); ++x) {
            entering.push_back(view.row(y).subspan(x, 1).front());
            result.facts.counts.eligible +=
                static_cast<std::uint64_t>(mask.subspan(x, 1).front() == 0);
            result.facts.counts.protected_samples +=
                static_cast<std::uint64_t>(mask.subspan(x, 1).front() != 0);
        }
    }
    return entering;
}
struct MorphologyReferenceSelection {
    std::uint32_t stride{};
    std::vector<std::size_t> points;
    bool fallback{};
    double fill{};
};
inline MorphologyReferenceSelection
morphology_reference_selection(MorphologyWorkingFixture& fixture,
                               std::span<const double> entering) {
    MorphologyReferenceSelection selection{
        .stride = (std::max(fixture.luminance.width(), fixture.luminance.height()) + 1023) / 1024,
        .points = {},
    };
    selection.points = morphology_selected_points(fixture, selection.stride);
    selection.fallback = selection.points.empty();
    if (selection.fallback) {
        selection.points = morphology_selected_points(fixture, 1);
        selection.points.resize(1);
    }
    std::vector<double> samples;
    samples.reserve(selection.points.size());
    for (const auto index : selection.points) {
        samples.push_back(entering[index]);
    }
    selection.fill = morphology_reference_rank(samples, 0.9);
    return selection;
}
inline void morphology_reference_fill(MorphologyWorkingFixture& fixture,
                                      std::vector<double>& entering, double fill) {
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        const auto mask = fixture.protection.view().row(y);
        for (std::uint32_t x = 0; x < fixture.luminance.width(); ++x) {
            if (mask.subspan(x, 1).front() != 0) {
                entering.at((std::size_t{y} * fixture.luminance.width()) + x) = fill;
            }
        }
    }
}
inline methods::MorphologyAnalysis
morphology_reference_statistics(const MorphologyReferenceSelection& selection,
                                std::span<const double> background, std::optional<double> target) {
    std::vector<double> samples;
    double minimum = 1;
    double maximum = 0;
    for (const auto value : background) {
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    samples.reserve(selection.points.size());
    for (const auto index : selection.points) {
        samples.push_back(background[index]);
    }
    const auto q90 = morphology_reference_rank(samples, 0.9);
    return {
        .stride = selection.stride,
        .count = static_cast<std::uint32_t>(selection.points.size()),
        .fallback = selection.fallback,
        .analysis_fill = selection.fill,
        .target = target.value_or(q90),
        .background_q10 = morphology_reference_rank(samples, 0.1),
        .background_q50 = morphology_reference_rank(samples, 0.5),
        .background_q90 = q90,
        .background_min = minimum,
        .background_max = maximum,
    };
}
inline MorphologyWorkingReference
morphology_working_reference(MorphologyWorkingFixture& fixture,
                             methods::MorphologyParameters parameters) {
    MorphologyWorkingReference result;
    auto entering = morphology_reference_input(fixture, result);
    const auto automatic = static_cast<unsigned>(
        std::floor((std::min(fixture.luminance.width(), fixture.luminance.height()) / 50.0) + 0.5));
    const auto radius = parameters.radius.value_or(std::clamp(automatic, 8U, 128U));
    result.facts.resolved_radius = radius;
    result.facts.sigma = static_cast<double>(radius) / 2;
    result.facts.gaussian_radius = static_cast<std::uint32_t>(std::ceil(3 * result.facts.sigma));
    if (parameters.strength == 0) {
        result.facts.reason = methods::MorphologyReason::zero_strength;
        return result;
    }
    if (parameters.max_gain == 1) {
        result.facts.reason = methods::MorphologyReason::unit_gain;
        return result;
    }
    if (result.facts.counts.eligible == 0) {
        return result;
    }
    const auto selection = morphology_reference_selection(fixture, entering);
    morphology_reference_fill(fixture, entering, selection.fill);
    const auto background = morphology_reference_field(
        entering, {.width = fixture.luminance.width(), .height = fixture.luminance.height()},
        radius);
    result.facts.analysis =
        morphology_reference_statistics(selection, background, parameters.target);
    morphology_reference_map(fixture, background, parameters, result);
    return result;
}
} // namespace docenhance::tests
