// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
core::Result<Tvl1> Tvl1::create(Tvl1Parameters p) {
    if (!std::isfinite(p.lambda) || p.lambda < minimum_lambda || p.lambda > maximum_lambda ||
        p.iterations < minimum_iterations || p.iterations > maximum_iterations ||
        !std::isfinite(p.tolerance) || p.tolerance < minimum_tolerance ||
        p.tolerance > maximum_tolerance || !std::isfinite(p.blend) || p.blend < 0 || p.blend > 1) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid TV-L1 fidelity, iteration cap, tolerance or blend");
    }
    return Tvl1{p};
}
namespace {
core::Result<image::Plane<double>> plane(const Tvl1Execution& e, image::Extent extent) {
    if (e.cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto value = image::Plane<double>::allocate(e.budget.get(), extent.width, extent.height);
    if (!value) {
        return std::unexpected(value.error());
    }
    e.report.get().preparation_charge_peak = std::max(
        e.report.get().preparation_charge_peak, static_cast<std::uint64_t>(e.budget.get().used()));
    return std::move(*value);
}
core::Result<void> counts(image::Extent extent, image::PlaneView<const std::uint8_t> mask,
                          const Tvl1Execution& e) {
    if (extent.width == 0 || extent.height == 0 ||
        (!mask.empty() && (extent.width != mask.width() || extent.height != mask.height()))) {
        return core::failure(core::ErrorCode::argument,
                             "TV-L1 source and protection extents differ");
    }
    const auto total = std::uint64_t{extent.width} * extent.height;
    if (total > image::source_pixels_max) {
        return core::failure(core::ErrorCode::resource, "TV-L1 pixel limit exceeded");
    }
    std::uint64_t protected_count = 0;
    for (std::uint32_t y = 0; y < mask.height(); ++y) {
        for (std::uint32_t x = 0; x < mask.width(); ++x) {
            constexpr std::uint32_t interval = 1024;
            if (x % interval == 0 && e.cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            protected_count += static_cast<std::uint64_t>(mask.row(y).subspan(x, 1).front() != 0);
        }
    }
    e.report.get().eligible_samples = total - protected_count;
    e.report.get().protected_samples = protected_count;
    return {};
}
core::Result<void> fill_block(Tvl1Field field, image::RowRange range, std::span<const double> rgb) {
    for (std::uint32_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
        const auto pixel = rgb.subspan(std::size_t{i} * image::rgb_channels, image::rgb_channels);
        auto y = image::luminance(
            {pixel.front(), pixel.subspan(1, 1).front(), pixel.subspan(2, 1).front()});
        if (!y) {
            return std::unexpected(y.error());
        }
        auto f = image::srgb_encode(*y);
        if (!f) {
            return std::unexpected(f.error());
        }
        const auto x = range.first + i;
        // The input view is const in the iteration capability; initialization owns its mutable
        // view.
        field.primal.row(range.row).subspan(x, 1).front() = *f;
        field.extrapolated.row(range.row).subspan(x, 1).front() = *f;
        field.dual_x.row(range.row).subspan(x, 1).front() = 0;
        field.dual_y.row(range.row).subspan(x, 1).front() = 0;
    }
    return {};
}
core::Result<void> fill(image::LinearSource& source, Tvl1Field field,
                        image::PlaneView<double> input, const Tvl1Execution& e) {
    auto block = plane(e, {.width = image::linear_block_pixels * image::rgb_channels, .height = 1});
    if (!block) {
        return std::unexpected(block.error());
    }
    for (std::uint32_t y = 0; y < input.height(); ++y) {
        for (std::uint32_t first = 0; first < input.width();) {
            if (e.cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            const auto count = std::min(image::linear_block_pixels, input.width() - first);
            const auto rgb = block->view().row(0).first(std::size_t{count} * image::rgb_channels);
            auto read = source.read({.row = y, .first = first}, rgb, image::RowUse::output);
            if (!read) {
                return read;
            }
            auto filled = fill_block(field, {.row = y, .first = first}, rgb);
            if (!filled) {
                return filled;
            }
            std::ranges::copy(field.primal.row(y).subspan(first, count),
                              input.row(y).subspan(first, count).begin());
            first += count;
        }
    }
    return {};
}
core::Result<void> solve(Tvl1Field field, const Tvl1& method, const Tvl1Execution& e) {
    auto& report = e.report.get().tvl1;
    if (!report) {
        return core::failure(core::ErrorCode::invariant, "TV-L1 solve has no diagnostics");
    }
    const auto parameters = method.parameters();
    auto start =
        tvl1_objective(field.input, field.primal.as_const(), parameters.lambda, e.cancellation);
    if (!start) {
        return std::unexpected(start.error());
    }
    report->objective_start = *start;
    for (std::uint32_t iteration = 1; iteration <= parameters.iterations; ++iteration) {
        auto updates = tvl1_iteration(field, parameters.lambda, e.cancellation);
        if (!updates) {
            return std::unexpected(updates.error());
        }
        report->iterations = iteration;
        report->primal_update = updates->primal;
        report->dual_update = updates->dual;
        if (iteration >= tvl1_first_checkpoint && iteration % tvl1_checkpoint_period == 0) {
            report->passing_checkpoints =
                updates->primal <= parameters.tolerance && updates->dual <= parameters.tolerance
                    ? report->passing_checkpoints + 1
                    : 0;
            if (report->passing_checkpoints == tvl1_required_passes) {
                report->stop = Tvl1Stop::tolerance_met;
                break;
            }
        }
    }
    if (!report->stop) {
        report->stop = Tvl1Stop::iteration_limit;
    }
    auto end =
        tvl1_objective(field.input, field.primal.as_const(), parameters.lambda, e.cancellation);
    if (!end) {
        return std::unexpected(end.error());
    }
    report->objective_end = *end;
    return {};
}
} // namespace
core::Result<Tvl1Model> Tvl1Model::prepare(image::LinearSource& source,
                                           image::PlaneView<const std::uint8_t> protection,
                                           const Tvl1& method, const Tvl1Execution& execution) {
    auto& report = execution.report.get();
    report = {.status = DenoiseStatus::failed, .requested = method.parameters()};
    if (execution.cancellation.requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    auto counted = counts(source.extent(), protection, execution);
    if (!counted) {
        return std::unexpected(counted.error());
    }
    report.tvl1.emplace();
    if (method.parameters().blend == 0 || report.eligible_samples == 0) {
        report.status = DenoiseStatus::no_change;
        report.reason = method.parameters().blend == 0 ? DenoiseReason::zero_blend
                                                       : DenoiseReason::no_eligible_samples;
        report.complete = true;
        return Tvl1Model{{}, {}, method.parameters().blend};
    }
    auto input = plane(execution, source.extent());
    if (!input) {
        return std::unexpected(input.error());
    }
    auto primal = plane(execution, source.extent());
    if (!primal) {
        return std::unexpected(primal.error());
    }
    auto extrapolated = plane(execution, source.extent());
    if (!extrapolated) {
        return std::unexpected(extrapolated.error());
    }
    auto px = plane(execution, source.extent());
    if (!px) {
        return std::unexpected(px.error());
    }
    auto py = plane(execution, source.extent());
    if (!py) {
        return std::unexpected(py.error());
    }
    const Tvl1Field field{
        .input = input->view().as_const(),
        .primal = primal->view(),
        .extrapolated = extrapolated->view(),
        .dual_x = px->view(),
        .dual_y = py->view(),
    };
    auto initialized = fill(source, field, input->view(), execution);
    if (!initialized) {
        return std::unexpected(initialized.error());
    }
    report.tvl1->field_bytes = input->size_bytes();
    auto solved = solve(field, method, execution);
    if (!solved) {
        return std::unexpected(solved.error());
    }
    report.status = DenoiseStatus::no_change;
    report.reason = DenoiseReason::no_effect;
    return Tvl1Model{std::move(*input), std::move(*primal), method.parameters().blend};
}
} // namespace docenhance::methods
