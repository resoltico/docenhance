// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "denoising.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "docenhance/opencv/nlm.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::host {
namespace {
core::Result<void> apply_nlm(const NlmPlanes& planes, image::RowRange range, std::span<double> rgb,
                             image::PlaneView<const std::uint8_t> protection,
                             methods::DenoisingReport& report) {
    const auto* const parameters =
        report.requested ? std::get_if<methods::NlmParameters>(&*report.requested) : nullptr;
    if (parameters == nullptr) {
        return core::failure(core::ErrorCode::invariant, "Prepared NLM requires its settings");
    }
    for (std::size_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
        const auto x = range.first + static_cast<std::uint32_t>(i);
        if (!protection.empty() && protection.row(range.row).subspan(x, 1).front() != 0) {
            continue;
        }
        const auto q = planes.input.view().row(range.row).subspan(x, 1).front();
        const auto qd = planes.output.view().row(range.row).subspan(x, 1).front();
        const auto offset = i * image::rgb_channels;
        const image::Rgb before{
            rgb.subspan(offset, 1).front(),
            rgb.subspan(offset + 1, 1).front(),
            rgb.subspan(offset + 2, 1).front(),
        };
        auto corrected = methods::nlm_correct(before, q, qd, parameters->blend);
        if (!corrected) {
            return std::unexpected(corrected.error());
        }
        {
            ++report.evaluated_samples;
            report.corrected_samples += static_cast<std::uint64_t>(q != qd);
            report.changed_samples += static_cast<std::uint64_t>(*corrected != before);
        }
        std::ranges::copy(*corrected, rgb.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return {};
}
} // namespace
bool active_denoising(const DenoisingPlanes& planes) {
    return std::visit(
        [](const auto& value) {
            using Planes = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Planes, NlmPlanes>) {
                return !value.input.empty();
            } else {
                return value.active();
            }
        },
        planes);
}
core::Result<void> DenoisedSource::read(image::RowRange range, std::span<double> rgb,
                                        image::RowUse use) {
    const auto& planes = planes_.get();
    const bool active = active_denoising(planes);
    auto read = source_.get().read(range, rgb, active ? image::RowUse::verification : use);
    if (!read || !active) {
        return read;
    }
    auto ignored = report_.get();
    auto& report = use == image::RowUse::output ? report_.get() : ignored;
    return std::visit(
        [&](const auto& value) -> core::Result<void> {
            using Planes = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Planes, NlmPlanes>) {
                return apply_nlm(value, range, rgb, protection_, report);
            } else {
                return value.apply(range, rgb, protection_, report, cancellation_);
            }
        },
        planes);
}

namespace {
core::Result<void> quantize_block(std::span<const double> rgb, std::span<std::uint16_t> values) {
    for (std::uint32_t i = 0; i < values.size(); ++i) {
        const auto offset = std::size_t{i} * image::rgb_channels;
        auto q = methods::nlm_quantize({
            rgb.subspan(offset, 1).front(),
            rgb.subspan(offset + 1, 1).front(),
            rgb.subspan(offset + 2, 1).front(),
        });
        if (!q) {
            return std::unexpected(q.error());
        }
        values.subspan(i, 1).front() = *q;
    }
    return {};
}
core::Result<void> quantize_source(image::LinearSource& source,
                                   image::PlaneView<std::uint16_t> input, core::Budget& budget,
                                   const core::Cancellation& cancellation) {
    const auto extent = source.extent();
    auto block =
        image::Plane<double>::allocate(budget, image::linear_block_pixels * image::rgb_channels, 1);
    if (!block) {
        return std::unexpected(block.error());
    }
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t first = 0; first < extent.width;) {
            if (cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            const auto count = std::min(image::linear_block_pixels, extent.width - first);
            auto const rgb = block->view().row(0).first(std::size_t{count} * image::rgb_channels);
            auto read = source.read({.row = y, .first = first}, rgb, image::RowUse::output);
            if (!read) {
                return std::unexpected(read.error());
            }
            auto quantized = quantize_block(rgb, input.row(y).subspan(first, count));
            if (!quantized) {
                return quantized;
            }
            first += count;
        }
    }
    return {};
}
} // namespace
core::Result<DenoisingPlanes> prepare_denoising(DenoisingInput source_input,
                                                const app::ProcessRequest& request,
                                                core::Budget& budget,
                                                const core::Cancellation& cancellation,
                                                methods::DenoisingReport& report) {
    auto& source = source_input.source.get();
    const auto protection = source_input.protection;
    if (const auto* const tv = std::get_if<methods::Tvl1>(&request.denoising())) {
        auto model =
            methods::Tvl1Model::prepare(source, protection, *tv, {budget, cancellation, report});
        if (!model) {
            return std::unexpected(model.error());
        }
        return DenoisingPlanes{std::move(*model)};
    }
    const auto* const method = std::get_if<methods::Nlm>(&request.denoising());
    if (method == nullptr) {
        report.complete = true;
        return DenoisingPlanes{};
    }
    report.requested = method->parameters();
    report.native_h = methods::nlm_native_strength(method->parameters());
    const auto parameters = method->parameters();
    report.status = methods::DenoiseStatus::failed;
    if (parameters.blend == 0 || report.eligible_samples == 0) {
        report.status = methods::DenoiseStatus::no_change;
        report.reason = parameters.blend == 0 ? methods::DenoiseReason::zero_blend
                                              : methods::DenoiseReason::no_eligible_samples;
        report.complete = true;
        return DenoisingPlanes{};
    }
    const auto extent = source.extent();
    if (parameters.search > std::min(extent.width, extent.height)) {
        return core::failure(core::ErrorCode::method_inapplicable,
                             "NLM search must fit the oriented image");
    }
    NlmPlanes planes;
    auto input = image::Plane<std::uint16_t>::allocate(budget, extent.width, extent.height);
    if (!input) {
        return std::unexpected(input.error());
    }
    planes.input = std::move(*input);
    auto output = image::Plane<std::uint16_t>::allocate(budget, extent.width, extent.height);
    if (!output) {
        return std::unexpected(output.error());
    }
    planes.output = std::move(*output);
    auto quantized = quantize_source(source, planes.input.view(), budget, cancellation);
    if (!quantized) {
        return std::unexpected(quantized.error());
    }
    auto run = opencv::denoise(
        planes.input.view().as_const(), planes.output.view(),
        {.method = *method, .budget = budget, .cancellation = cancellation, .report = report});
    if (!run) {
        return std::unexpected(run.error());
    }
    return DenoisingPlanes{std::move(planes)};
}
core::Result<void> assess_denoising(DenoisedSource& source, core::Budget& budget,
                                    const core::Cancellation& cancellation,
                                    methods::DenoisingReport& report) {
    auto block =
        image::Plane<double>::allocate(budget, image::linear_block_pixels * image::rgb_channels, 1);
    if (!block) {
        return std::unexpected(block.error());
    }
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t first = 0; first < source.extent().width;) {
            if (cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            const auto count = std::min(image::linear_block_pixels, source.extent().width - first);
            auto read =
                source.read({.row = y, .first = first},
                            block->view().row(0).first(std::size_t{count} * image::rgb_channels),
                            image::RowUse::output);
            if (!read) {
                return read;
            }
            first += count;
        }
    }
    report.status = (report.changed_samples != 0U) ? methods::DenoiseStatus::applied
                                                   : methods::DenoiseStatus::no_change;
    report.reason = (report.changed_samples != 0U) ? methods::DenoiseReason::none
                                                   : methods::DenoiseReason::no_effect;
    report.complete = true;
    return {};
}
} // namespace docenhance::host
