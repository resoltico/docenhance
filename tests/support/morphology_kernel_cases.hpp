// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "gamma_fixture.hpp"
#include "illumination_receiver_cases.hpp"
#include "morphology_reference.hpp"
#include "morphology_working_detail.hpp"
#include "require.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace docenhance::tests {
inline constexpr std::uint32_t morphology_baseline_width = 1031;
inline constexpr std::uint32_t morphology_baseline_radius = 256;
inline double morphology_baseline_sample(std::uint32_t x) {
    if (x == 255) {
        return 0.125;
    }
    return x < 600 ? 0.25 : 0.75;
}
inline void require_morphology_unit(image::PlaneView<const double> field) {
    for (std::uint32_t y = 0; y < field.height(); ++y) {
        require(std::ranges::all_of(
                    field.row(y),
                    [](double value) { return std::isfinite(value) && value >= 0 && value <= 1; }),
                "every actual logical output is finite Unit before the next actual pass");
        require(std::ranges::all_of(
                    field.storage().subspan((std::size_t{y} * field.row_pitch()) + field.width(),
                                            field.row_pitch() - field.width()),
                    [](double value) { return value == -1; }),
                "actual passes preserve initialized row padding");
    }
}
inline std::vector<double> morphology_max_weights() {
    std::vector<double> weights(769);
    for (std::size_t k = 0; k < weights.size(); ++k) {
        const auto offset = static_cast<double>(k) - 384;
        weights.at(k) = std::exp(-(offset * offset) / (2 * 128.0 * 128.0));
    }
    return weights;
}
inline double morphology_weight_sum(std::span<const double> weights) {
    double sum = 0;
    for (const auto weight : weights) {
        sum += weight;
    }
    return sum;
}
// Independent singleton-axis brute force; no deque, rolling extrema or production folding.
inline std::vector<double> morphology_line_reference(std::span<const double> input, bool dilation) {
    std::vector<double> result(input.size());
    for (std::uint32_t x = 0; x < input.size(); ++x) {
        double value = dilation ? 0 : 1;
        for (std::int64_t dx = -256; dx <= 256; ++dx) {
            const auto index =
                morphology_reflect(std::int64_t{x} + dx, static_cast<std::uint32_t>(input.size()));
            value = dilation ? std::max(value, input[index]) : std::min(value, input[index]);
        }
        result.at(x) = value;
    }
    return result;
}
inline double& morphology_line_at(image::PlaneView<double> field, std::uint32_t i) {
    return field.width() == 1 ? field.row(i).front() : field.row(0).subspan(i, 1).front();
}
inline MorphologyReferencePoint morphology_line_point(bool vertical, std::uint32_t i) {
    return {.x = vertical ? 0 : i, .y = vertical ? i : 0};
}
inline void morphology_closing_passes(image::Plane<double>& field, image::Plane<double>& scratch,
                                      image::Plane<std::uint64_t>& queue) {
    const image::Extent extent{.width = field.width(), .height = field.height()};
    for (const bool dilating : {true, false}) {
        for (const bool horizontal : {true, false}) {
            const auto before = gamma_bits(field.view().storage());
            require(
                methods::extrema_pass(field.view().as_const(), scratch.view(), queue.view().row(0),
                                      {
                                          .radius = morphology_baseline_radius,
                                          .horizontal = horizontal,
                                          .dilation = dilating,
                                          .first_line = 0,
                                          .last_line = horizontal ? extent.height : extent.width,
                                      })
                    .has_value(),
                "canonical maximal-radius extrema succeeds on its producer-admitted state");
            require(gamma_bits(field.view().storage()) == before,
                    "actual extrema pass leaves its complete readonly source unchanged");
            require_morphology_unit(scratch.view().as_const());
            std::swap(field, scratch);
        }
    }
}
inline void morphology_smoothing_passes(image::Plane<double>& field,
                                        image::Plane<double>& scratch) {
    const image::Extent extent{.width = field.width(), .height = field.height()};
    const auto weights = morphology_max_weights();
    const auto normalization = morphology_weight_sum(weights);
    for (const bool horizontal : {true, false}) {
        const auto before = gamma_bits(field.view().storage());
        require(image::gaussian_pass(field.view().as_const(), scratch.view(),
                                     {
                                         .weights = weights,
                                         .normalization = normalization,
                                         .horizontal = horizontal,
                                         .first_row = 0,
                                         .last_row = extent.height,
                                     })
                    .has_value(),
                "actual maximal-support Gaussian pass follows the completed closing joins");
        require(gamma_bits(field.view().storage()) == before,
                "actual Gaussian pass leaves its complete readonly source unchanged");
        require_morphology_unit(scratch.view().as_const());
        std::swap(field, scratch);
    }
}
inline void morphology_maximum_field_case(bool vertical) {
    core::Budget budget{illumination_boundary_budget};
    const image::Extent extent{
        .width = vertical ? 1 : morphology_baseline_width,
        .height = vertical ? morphology_baseline_width : 1,
    };
    auto field_result = image::Plane<double>::allocate(budget, extent.width, extent.height);
    require(field_result.has_value(), "maximal field allocates");
    auto field = std::move(*field_result);
    auto scratch_result = image::Plane<double>::allocate(budget, extent.width, extent.height);
    require(scratch_result.has_value(), "maximal scratch allocates");
    auto scratch = std::move(*scratch_result);
    auto queue_result = image::Plane<std::uint64_t>::allocate(budget, 513, 1);
    require(queue_result.has_value(), "maximal queue allocates");
    auto queue = std::move(*queue_result);
    std::ranges::fill(field.view().storage(), -1);
    std::ranges::fill(scratch.view().storage(), -1);
    std::vector<double> entering;
    for (std::uint32_t i = 0; i < morphology_baseline_width; ++i) {
        const auto value = morphology_baseline_sample(i);
        entering.push_back(value);
        morphology_line_at(field.view(), i) = value;
    }
    const auto dilation = morphology_line_reference(entering, true);
    const auto closing = morphology_line_reference(dilation, false);
    require(closing.front() != closing.back(),
            "maximal-radius fixture retains a nonconstant field");
    require_morphology_unit(field.view().as_const());
    morphology_closing_passes(field, scratch, queue);
    for (std::uint32_t i = 0; i < morphology_baseline_width; ++i) {
        require(morphology_line_at(field.view(), i) == closing.at(i),
                "four actual closing passes match independent nonconstant singleton-axis extrema");
    }
    morphology_smoothing_passes(field, scratch);
    for (const auto i : {0U, 255U, 256U, 511U, 512U, 768U, 1024U, 1030U}) {
        const auto expected =
            gaussian_sample(closing, {.width = extent.width, .height = extent.height},
                            morphology_line_point(vertical, i), 256);
        require(
            std::abs(morphology_line_at(field.view(), i) - expected) < 1e-12,
            "maximal support at endpoints/seams agrees with independent two-dimensional Gaussian");
    }
}
inline void morphology_interior_pass_case(bool gaussian) {
    core::Budget budget{illumination_boundary_budget};
    auto input_result = image::Plane<double>::allocate(budget, morphology_baseline_width, 1);
    require(input_result.has_value(), "interior input allocates");
    auto input = std::move(*input_result);
    auto output_result = image::Plane<double>::allocate(budget, morphology_baseline_width, 1);
    require(output_result.has_value(), "interior output allocates");
    auto output = std::move(*output_result);
    auto queue_result = image::Plane<std::uint64_t>::allocate(budget, 513, 1);
    require(queue_result.has_value(), "maximal queue allocates");
    auto queue = std::move(*queue_result);
    std::ranges::fill(input.view().storage(), -1);
    for (std::uint32_t x = 0; x < input.width(); ++x) {
        input.view().row(0).subspan(x, 1).front() = morphology_baseline_sample(x);
    }
    std::ranges::fill(output.view().storage(), -1);
    const auto before = gamma_bits(input.view().storage());
    const auto weights = morphology_max_weights();
    const auto allowed = gaussian ? std::size_t{1} : std::size_t{3};
    const CheckpointStop stop{core::Checkpoint::processing, allowed};
    const auto result =
        gaussian ? image::gaussian_pass(input.view().as_const(), output.view(),
                                        {
                                            .weights = weights,
                                            .normalization = morphology_weight_sum(weights),
                                            .first_row = 0,
                                            .last_row = 1,
                                        },
                                        stop.cancellation())
                 : methods::extrema_pass(
                       input.view().as_const(), output.view(), queue.view().row(0),
                       {.radius = morphology_baseline_radius, .first_line = 0, .last_line = 1},
                       stop.cancellation());
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1 &&
                gamma_bits(input.view().storage()) == before,
            "real inner Gaussian x128 or extrema p768 stops at its attained existing checkpoint");
    const auto written = gaussian ? 128U : 256U;
    require(std::ranges::all_of(output.view().row(0).first(written),
                                [](double value) { return std::isfinite(value) && value >= 0; }) &&
                std::ranges::all_of(output.view().storage().subspan(written),
                                    [](double value) { return value == -1; }),
            "real pass keeps a written valid prefix and exact untouched suffix/padding");
    require((gaussian ? gaussian_sample(input.view().row(0), {.width = input.width(), .height = 1},
                                        {.x = written, .y = 0}, 256)
                      : morphology_line_reference(input.view().row(0), true).at(written)) >= 0,
            "independent late destination would replace its negative sentinel without the stop");
}
inline void morphology_maximum_fit_case(std::optional<std::size_t> allowed) {
    core::Budget owner{illumination_boundary_budget};
    auto source = image::Plane<double>::allocate(owner, morphology_baseline_width, 1);
    require(source.has_value(), "maximal scalar source allocates");
    std::ranges::fill(source->view().storage(), -1);
    for (std::uint32_t x = 0; x < morphology_baseline_width; ++x) {
        source->view().row(0).subspan(x, 1).front() = morphology_baseline_sample(x);
    }
    const auto original = gamma_bits(source->view().storage());
    core::Budget budget{illumination_boundary_budget};
    const auto method = methods::Morphology::create({.radius = 256});
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(method.has_value() && concurrency.has_value(), "maximal fit parameters admit");
    const auto limit = allowed ? *allowed : std::numeric_limits<std::size_t>::max();
    const CheckpointStop stop{core::Checkpoint::processing, limit};
    const exec::Scheduler scheduler{*concurrency, stop.cancellation()};
    {
        const auto fit = methods::fit_morphology(*method, {source->view(), {}, budget, scheduler},
                                                 morphology_baseline_width);
        if (allowed) {
            require(!fit && fit.error().code == core::ErrorCode::cancelled &&
                        CheckpointStop::visits() == *allowed + 1 && budget.used() == 0,
                    "actual maximal phase interior failure returns no fit and refunds");
        } else {
            require(fit.has_value() && CheckpointStop::visits() == 6218 && budget.used() > 0,
                    "healthy actual maximal phase reaches early and late interior controls");
            const auto field = fit->background.view().row(0);
            require(std::isfinite(field.front()) && std::isfinite(field.back()) &&
                        std::abs(field.front() - 0.25) < 1e-12 &&
                        std::abs(field.back() - 0.75) < 1e-12,
                    "actual maximal phase retains its genuinely nonconstant background");
        }
    }
    require(budget.used() == 0 && gamma_bits(source->view().storage()) == original,
            "every actual fit exit leaves source exact and refunds owned phase storage");
}
inline void morphology_kernel_baseline_cases() {
    morphology_maximum_field_case(false);
    morphology_maximum_field_case(true);
    morphology_interior_pass_case(false);
    morphology_interior_pass_case(true);
    morphology_maximum_fit_case(std::nullopt);
    morphology_maximum_fit_case(3);
    morphology_maximum_fit_case(6201);
}
} // namespace docenhance::tests
