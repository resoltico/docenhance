// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
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
bool fill_row(Working working, image::PlaneView<double> field, std::uint32_t y,
              double value) noexcept {
    const auto row = field.row(y);
    const auto source = working.luminance.row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    for (std::size_t x = 0; x < row.size(); ++x) {
        constexpr std::size_t interval = 1024;
        if (x % interval == 0 &&
            working.scheduler.get().cancellation().requested(core::Checkpoint::measurement)) {
            return false;
        }
        row.subspan(x, 1).front() = (!mask.empty() && mask.subspan(x, 1).front() != 0)
                                        ? value
                                        : source.subspan(x, 1).front();
    }
    return true;
}
core::Result<void> fill(Working working, image::PlaneView<double> field, double value) {
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = morphology_lines(field.height(), band);
        for (auto y = first; y < last; ++y) {
            if (!fill_row(working, field, y, value)) {
                return core::cancelled();
            }
        }
        return {};
    };
    return working.scheduler.get().for_each(morphology_bands(field.height()), exec::WorkRef{work});
}
core::Result<void> extrema(Working working, image::PlaneView<const double> input,
                           image::PlaneView<double> output, image::PlaneView<std::uint64_t> queues,
                           ExtremaPass descriptor) {
    const auto lines = descriptor.horizontal ? input.height() : input.width();
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = morphology_lines(lines, band);
        auto pass = descriptor;
        pass.first_line = first;
        pass.last_line = last;
        return extrema_pass(input, output, queues.row(static_cast<std::uint32_t>(band)), pass,
                            working.scheduler.get().cancellation());
    };
    return working.scheduler.get().for_each(morphology_bands(lines), exec::WorkRef{work});
}
core::Result<void> closing(Working working, image::Plane<double>& field,
                           image::Plane<double>& scratch, std::uint32_t radius) {
    auto queues = morphology_allocate<std::uint64_t>(
        working, (2 * radius) + 1,
        static_cast<std::uint32_t>(morphology_bands(std::max(field.width(), field.height()))));
    if (!queues) {
        return std::unexpected(std::move(queues.error()));
    }
    for (const bool dilation : {true, false}) {
        for (const bool horizontal : {true, false}) {
            if (auto passed =
                    extrema(working, field.view().as_const(), scratch.view(), queues->view(),
                            {.radius = radius, .horizontal = horizontal, .dilation = dilation});
                !passed) {
                return std::unexpected(std::move(passed.error()));
            }
            std::swap(field, scratch);
        }
    }
    return {};
}
core::Result<void> gaussian(Working working, image::PlaneView<const double> input,
                            image::PlaneView<double> output, image::GaussianPass descriptor) {
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = morphology_lines(input.height(), band);
        auto pass = descriptor;
        pass.first_row = first;
        pass.last_row = last;
        return image::gaussian_pass(input, output, pass, working.scheduler.get().cancellation());
    };
    return working.scheduler.get().for_each(morphology_bands(input.height()), exec::WorkRef{work});
}
core::Result<void> smooth(Working working, image::Plane<double>& field,
                          image::Plane<double>& scratch, std::uint32_t radius) {
    const auto support = morphology_gaussian_radius(radius);
    auto coefficients = morphology_allocate<double>(working, (2 * support) + 1, 1);
    if (!coefficients) {
        return std::unexpected(std::move(coefficients.error()));
    }
    const auto weights = coefficients->view().row(0);
    const auto normalization =
        morphology_gaussian_weights(weights, morphology_sigma(radius), support);
    for (const bool horizontal : {true, false}) {
        if (auto passed = gaussian(
                working, field.view().as_const(), scratch.view(),
                {.weights = weights, .normalization = normalization, .horizontal = horizontal});
            !passed) {
            return std::unexpected(std::move(passed.error()));
        }
        std::swap(field, scratch);
    }
    return {};
}
core::Result<image::Plane<double>> background(Working working, std::uint32_t radius, double value) {
    auto field =
        morphology_allocate<double>(working, working.luminance.width(), working.luminance.height());
    if (!field) {
        return std::unexpected(std::move(field.error()));
    }
    auto scratch =
        morphology_allocate<double>(working, working.luminance.width(), working.luminance.height());
    if (!scratch) {
        return std::unexpected(std::move(scratch.error()));
    }
    if (auto filled = fill(working, field->view(), value); !filled) {
        return std::unexpected(std::move(filled.error()));
    }
    if (auto closed = closing(working, *field, *scratch, radius); !closed) {
        return std::unexpected(std::move(closed.error()));
    }
    if (auto smoothed = smooth(working, *field, *scratch, radius); !smoothed) {
        return std::unexpected(std::move(smoothed.error()));
    }
    return std::move(*field);
}
} // namespace
core::Result<MorphologyFit> fit_morphology(const Morphology& method, Working working,
                                           std::uint64_t eligible) {
    auto source = analyze_morphology(working, eligible);
    if (!source) {
        return std::unexpected(std::move(source.error()));
    }
    const auto radius = morphology_radius(
        {.width = working.luminance.width(), .height = working.luminance.height()}, method);
    auto field = background(working, radius, source->fill);
    if (!field) {
        return std::unexpected(std::move(field.error()));
    }
    auto analysis = measure_morphology(working, field->view().as_const(), *source, eligible);
    if (!analysis) {
        return std::unexpected(std::move(analysis.error()));
    }
    analysis->target = method.parameters().target.value_or(analysis->background_q90);
    return MorphologyFit{.background = std::move(*field), .analysis = *analysis};
}
} // namespace docenhance::methods
