// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "morphology_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
core::Result<Morphology> Morphology::create(MorphologyParameters p) {
    constexpr double target_min = 0.1;
    if (!std::isfinite(p.strength) || p.strength < 0 || p.strength > 1 ||
        !std::isfinite(p.max_gain) || p.max_gain < 1 || p.max_gain > illumination_gain_limit ||
        (p.target && (!std::isfinite(*p.target) || *p.target < target_min || *p.target > 1)) ||
        (p.radius && (*p.radius < min_radius || *p.radius > max_radius))) {
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
namespace {
core::Result<double> analysis_fill(MorphologyContext context) {
    const auto extent = context.input.source.get().extent();
    const auto capacity = static_cast<std::uint32_t>(std::min(
        std::uint64_t{extent.width} * extent.height, std::uint64_t{illumination_sample_limit}));
    auto values = morphology_plane<double>(context, capacity);
    if (!values) {
        return std::unexpected(values.error());
    }
    auto rgb = morphology_plane<double>(context, image::linear_block_pixels * image::rgb_channels);
    if (!rgb) {
        return std::unexpected(rgb.error());
    }
    auto sampled = morphology_samples(context, values->view().row(0), rgb->view().row(0));
    if (!sampled) {
        return std::unexpected(sampled.error());
    }
    if (!context.report.get().morphology) {
        return core::failure(core::ErrorCode::argument, "Missing I02 preparation observations");
    }
    auto& detail = context.report.get().morphology.value();
    detail.count = sampled->count;
    detail.stride = sampled->stride;
    detail.fallback = sampled->fallback;
    constexpr double quantile = 0.9;
    return image::nearest_rank(values->view().row(0).first(sampled->count), quantile,
                               context.cancellation.get());
}
double gaussian_weights(std::span<double> weights, const MorphologyMeasurements& detail) {
    double normalization = 0;
    for (std::uint32_t k = 0; k < weights.size(); ++k) {
        const auto offset = static_cast<double>(k) - detail.gaussian_radius;
        const auto value = std::exp(-(offset * offset) / (2 * detail.sigma * detail.sigma));
        weights.subspan(k, 1).front() = value;
        normalization += value;
    }
    return normalization;
}
core::Result<image::Plane<double>> prepare_field(MorphologyContext context) {
    const auto extent = context.input.source.get().extent();
    if (!context.report.get().morphology) {
        return core::failure(core::ErrorCode::argument, "Missing I02 preparation observations");
    }
    const auto& detail = context.report.get().morphology.value();
    auto field = morphology_plane<double>(context, extent.width, extent.height);
    if (!field) {
        return std::unexpected(field.error());
    }
    auto scratch = morphology_plane<double>(context, extent.width, extent.height);
    if (!scratch) {
        return std::unexpected(scratch.error());
    }
    auto queue = morphology_plane<std::uint64_t>(context, (2 * detail.radius) + 1);
    if (!queue) {
        return std::unexpected(queue.error());
    }
    auto kernel = morphology_plane<double>(context, (2 * detail.gaussian_radius) + 1);
    if (!kernel) {
        return std::unexpected(kernel.error());
    }
    auto rgb = morphology_plane<double>(context, image::linear_block_pixels * image::rgb_channels);
    if (!rgb) {
        return std::unexpected(rgb.error());
    }
    if (!detail.analysis_fill) {
        return core::failure(core::ErrorCode::argument, "Missing I02 analysis fill");
    }
    auto filled =
        morphology_fill(context, field->view(), rgb->view().row(0), *detail.analysis_fill);
    if (!filled) {
        return std::unexpected(filled.error());
    }
    for (const bool dilation : {true, false}) {
        for (const bool horizontal : {true, false}) {
            auto pass = extrema_pass(
                field->view().as_const(), scratch->view(), queue->view().row(0),
                {.radius = detail.radius, .horizontal = horizontal, .dilation = dilation},
                context.cancellation.get());
            if (!pass) {
                return std::unexpected(pass.error());
            }
            std::swap(*field, *scratch);
        }
    }
    const auto weights = kernel->view().row(0);
    const auto normalization = gaussian_weights(weights, detail);
    for (const bool horizontal : {true, false}) {
        auto pass = gaussian_pass(
            field->view().as_const(), scratch->view(),
            {.weights = weights, .normalization = normalization, .horizontal = horizontal},
            context.cancellation.get());
        if (!pass) {
            return std::unexpected(pass.error());
        }
        std::swap(*field, *scratch);
    }
    return std::move(*field);
}
} // namespace
core::Result<MorphologyModel>
MorphologyModel::prepare(IlluminationInput input, const Morphology& method, core::Budget& budget,
                         const core::Cancellation& cancellation, IlluminationReport& report) {
    report = {.status = IlluminationStatus::failed, .requested = method.parameters()};
    if (cancellation.requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    const auto extent = input.source.get().extent();
    const auto radius = morphology_radius(extent, method);
    constexpr std::uint32_t gaussian_support_sigmas = 3;
    report.morphology = MorphologyMeasurements{
        .radius = radius,
        .sigma = static_cast<double>(radius) / 2,
        .gaussian_radius = ((gaussian_support_sigmas * radius) + 1) / 2,
    };
    const MorphologyContext context{input, budget, cancellation, report};
    auto counted = morphology_counts(context);
    if (!counted) {
        return std::unexpected(counted.error());
    }
    const auto p = method.parameters();
    if (p.strength == 0 || p.max_gain == 1 || report.eligible_samples == 0) {
        report.status = IlluminationStatus::no_change;
        report.reason = IlluminationReason::no_eligible_samples;
        if (p.max_gain == 1) {
            report.reason = IlluminationReason::unit_gain;
        }
        if (p.strength == 0) {
            report.reason = IlluminationReason::zero_strength;
        }
        report.complete = true;
        return MorphologyModel{extent, {}, method, 0, false};
    }
    constexpr std::uint64_t minimum_samples = 16;
    if (report.eligible_samples <
        std::min(minimum_samples, std::uint64_t{extent.width} * extent.height)) {
        return core::failure(core::ErrorCode::method_inapplicable,
                             "I02 has too few eligible samples");
    }
    auto fill = analysis_fill(context);
    if (!fill) {
        return std::unexpected(fill.error());
    }
    report.morphology->analysis_fill = *fill;
    auto field = prepare_field(context);
    if (!field) {
        return std::unexpected(field.error());
    }
    report.morphology->field_bytes = field->size_bytes();
    auto stats = morphology_background_statistics(context, field->view().as_const());
    if (!stats) {
        return std::unexpected(stats.error());
    }
    const double target = p.target.value_or(report.morphology->background_q90);
    report.morphology->target = target;
    report.status = IlluminationStatus::no_change;
    report.reason = IlluminationReason::no_effect;
    return MorphologyModel{extent, std::move(*field), method, target, true};
}
core::Result<double> MorphologyModel::background(std::uint32_t x, std::uint32_t y) const {
    if (!active_ || x >= field_.width() || y >= field_.height()) {
        return core::failure(core::ErrorCode::argument, "Coordinate has no I02 background field");
    }
    return field_.view().row(y).subspan(x, 1).front();
}
} // namespace docenhance::methods
