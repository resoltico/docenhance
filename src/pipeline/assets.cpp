// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_output.hpp"
#include "docenhance/codec/protection_png.hpp"
#include "docenhance/codec/psf_png.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/pipeline/render.hpp"
#include "frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::pipeline {
namespace {
core::Result<methods::ResolvedPsf> analytic_psf(const methods::Wiener& method) {
    const auto& selection = method.parameters().psf;
    const auto side = methods::psf_side(selection);
    if (!side) {
        return std::unexpected(side.error());
    }
    methods::ResolvedPsf psf{
        .width = *side,
        .height = *side,
        .coefficients = std::vector<double>(std::size_t{*side} * (*side)),
    };
    const auto* const gaussian = std::get_if<methods::GaussianPsf>(&selection);
    const auto* const motion = std::get_if<methods::MotionPsf>(&selection);
    if (gaussian == nullptr && motion == nullptr) {
        return core::failure(core::ErrorCode::invariant, "Analytic PSF has no analytic selection");
    }
    const auto generated = gaussian != nullptr ? methods::gaussian_psf(*gaussian, psf.coefficients)
                                               : methods::motion_psf(*motion, psf.coefficients);
    if (!generated) {
        return std::unexpected(generated.error());
    }
    const auto centroid = methods::psf_centroid(psf.width, psf.height, psf.coefficients);
    if (!centroid) {
        return std::unexpected(centroid.error());
    }
    psf.centroid_x = centroid->front();
    psf.centroid_y = centroid->back();
    return psf;
}
core::Result<methods::ResolvedPsf> kernel_psf(Input& input, core::Budget& budget,
                                              const core::Cancellation& cancellation) {
    auto decoded = codec::decode_psf_png(input.encoded().psf.bytes(), budget, cancellation);
    if (!decoded) {
        return std::unexpected(std::move(decoded.error()));
    }
    const auto shape = decoded->shape;
    methods::ResolvedPsf result{
        .width = shape.width,
        .height = shape.height,
        .coefficients = std::vector<double>(std::size_t{shape.width} * shape.height),
        .source_identity = input.encoded().psf_identity,
    };
    const auto sample_bytes = shape.depth.bytes();
    for (std::uint32_t y = 0; y < shape.height; ++y) {
        if (cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        const auto row = decoded->pixels.view().row(y);
        for (std::uint32_t x = 0; x < shape.width; ++x) {
            result.coefficients.at((std::size_t{y} * shape.width) + x) = image::read_sample(
                row.subspan(std::size_t{x} * sample_bytes, sample_bytes), shape.depth);
        }
    }
    if (const auto normalized =
            methods::normalize_psf(shape.width, shape.height, result.coefficients);
        !normalized) {
        return core::failure(core::ErrorCode::input,
                             "PSF coefficients require a finite positive sum");
    }
    const auto centroid = methods::psf_centroid(shape.width, shape.height, result.coefficients);
    if (!centroid) {
        return std::unexpected(centroid.error());
    }
    result.centroid_x = centroid->front();
    result.centroid_y = centroid->back();
    return result;
}
} // namespace
core::Result<void> prepare_mask(Input& input, const methods::Plan& plan, Frame& frame,
                                core::Budget& budget, const exec::Scheduler& scheduler) {
    if (!plan.protection) {
        return {};
    }
    const auto& report = frame.rendering.interpretation;
    const auto shape = image::oriented_shape(report.decoded, report.orientation);
    auto mask = codec::decode_protection_png(input.encoded().mask.bytes(),
                                             {.width = shape.width, .height = shape.height}, budget,
                                             scheduler.cancellation());
    if (!mask) {
        return std::unexpected(std::move(mask.error()));
    }
    input.encoded().mask = core::Buffer{};
    const codec::PngImage intended{
        .shape =
            {
                .width = shape.width,
                .height = shape.height,
                .depth = image::PngDepth(),
            },
        .pixels = mask->view().as_const(),
        .profile = std::span<const std::byte>(),
        .resolution = std::nullopt,
    };
    auto encoded = codec::encode_png(intended, budget, scheduler.cancellation());
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    if (auto compared =
            codec::compare_png(encoded->bytes(), intended, budget, scheduler.cancellation());
        !compared) {
        return compared;
    }
    frame.rendering.mask = std::move(*encoded);
    if (plan.rotation == image::QuarterTurn::identity()) {
        frame.protection = std::move(*mask);
        return {};
    }
    auto rotated =
        image::Plane<std::uint8_t>::allocate(budget, report.output.width, report.output.height);
    if (!rotated) {
        return std::unexpected(std::move(rotated.error()));
    }
    if (auto applied = image::rotate_plane(mask->view().as_const(), rotated->view(), plan.rotation,
                                           scheduler.cancellation());
        !applied) {
        return applied;
    }
    frame.protection = std::move(*rotated);
    return {};
}
core::Result<void> resolve_psf(Input& input, const methods::Plan& plan, Frame& frame,
                               core::Budget& budget, const exec::Scheduler& scheduler) {
    const auto* const method = std::get_if<methods::Wiener>(&plan.photometric.restoration);
    if (method == nullptr) {
        return core::failure(core::ErrorCode::invariant,
                             "PSF resolution requires an entered restoration stage");
    }
    const bool kernel = std::holds_alternative<methods::Kernel>(method->parameters().psf);
    auto psf = kernel ? kernel_psf(input, budget, scheduler.cancellation()) : analytic_psf(*method);
    if (!psf) {
        return std::unexpected(std::move(psf.error()));
    }
    frame.failure.warning_context.resolved_psf_centroid =
        std::array{psf->centroid_x, psf->centroid_y};
    frame.psf = std::move(*psf);
    if (kernel) {
        auto& identity = input.encoded().psf_identity;
        if (!identity) {
            return core::failure(core::ErrorCode::invariant, "Supplied PSF identity is absent");
        }
        // kernel_psf has returned: all encoded-byte and decoded-sample borrows have died.
        // Move the SAME charged Buffer into the asset; no copy, re-encode or charge cycling.
        frame.rendering.psf = PsfAsset{
            .encoded = std::move(input.encoded().psf),
            .identity = std::move(*identity),
        };
        identity.reset();
    }
    return {};
}
} // namespace docenhance::pipeline
