// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "diagnostics.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/opencv/restoration.hpp"
#include "restoration_work.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <new>
#include <opencv2/core.hpp>
#include <opencv2/core/exception.hpp>
#include <utility>
#include <variant>
namespace docenhance::opencv {
namespace {
core::Result<std::uint32_t> optimal(std::uint64_t minimum) {
    if (minimum > static_cast<std::uint64_t>(std::numeric_limits<int>::max() / 2)) {
        return core::failure(core::ErrorCode::resource,
                             "Restoration FFT extent exceeds native domain");
    }
    try {
        configure_diagnostics();
        const auto value = cv::getOptimalDFTSize(static_cast<int>(minimum));
        if (value < 0 || std::cmp_less(value, minimum) ||
            value > std::numeric_limits<int>::max() / 2) {
            return core::failure(core::ErrorCode::resource, "Restoration FFT extent unavailable");
        }
        return static_cast<std::uint32_t>(value);
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource, "Restoration native allocation failed");
    } catch (const cv::Exception&) {
        return core::failure(core::ErrorCode::numerical, "Restoration FFT extent failed");
    }
}
template <typename T>
core::Result<image::Plane<T>> allocate(std::uint32_t width, std::uint32_t height,
                                       const RestorationExecution& e) {
    if (e.cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto plane = image::Plane<T>::allocate(e.budget.get(), width, height);
    if (plane) {
        auto& peak = e.report.get().preparation_charge_peak;
        peak = std::max(peak, static_cast<std::uint64_t>(e.budget.get().used()));
    }
    return plane;
}
} // namespace
core::Result<RestorationWork> restoration_work(image::Extent extent,
                                               const methods::ResolvedPsf& psf,
                                               const RestorationExecution& e) {
    const auto guard =
        std::max(methods::restoration_minimum_guard, methods::restoration_guard_radius_factor *
                                                         std::max(psf.width / 2, psf.height / 2));
    auto width = optimal(std::uint64_t{extent.width} + (std::uint64_t{2} * guard));
    auto height = optimal(std::uint64_t{extent.height} + (std::uint64_t{2} * guard));
    if (!width) {
        return std::unexpected(width.error());
    }
    if (!height) {
        return std::unexpected(height.error());
    }
    auto& report = e.report.get();
    report.guard = guard;
    report.fft_width = *width;
    report.fft_height = *height;
    auto charge = methods::restoration_charge(extent, {.width = *width, .height = *height});
    if (!charge) {
        return std::unexpected(charge.error());
    }
    if (charge->peak > e.budget.get().available()) {
        return core::failure(core::ErrorCode::resource,
                             "Restoration charged-buffer budget refused");
    }
    if (e.cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    // All owned arrays and the native lease are planned before sample reads or processing writes.
    auto native = e.budget.get().reserve(static_cast<std::size_t>(charge->native));
    if (!native) {
        return std::unexpected(native.error());
    }
    report.native_reserved_bytes = native->size();
    report.preparation_charge_peak = e.budget.get().used();
    auto candidate = allocate<double>(extent.width, extent.height, e);
    if (!candidate) {
        return std::unexpected(candidate.error());
    }
    auto transfer = allocate<double>(image::linear_block_pixels * image::rgb_channels, 1, e);
    if (!transfer) {
        return std::unexpected(transfer.error());
    }
    auto input = allocate<float>(*width, *height, e);
    if (!input) {
        return std::unexpected(input.error());
    }
    auto kernel = allocate<float>(*width, *height, e);
    if (!kernel) {
        return std::unexpected(kernel.error());
    }
    auto spectrum = allocate<float>(2 * (*width), *height, e);
    if (!spectrum) {
        return std::unexpected(spectrum.error());
    }
    auto kernel_spectrum = allocate<float>(2 * (*width), *height, e);
    if (!kernel_spectrum) {
        return std::unexpected(kernel_spectrum.error());
    }
    report.preparation_charge_peak = e.budget.get().used();
    return RestorationWork{
        .candidate = std::move(*candidate),
        .transfer = std::move(*transfer),
        .input = std::move(*input),
        .kernel = std::move(*kernel),
        .spectrum = std::move(*spectrum),
        .kernel_spectrum = std::move(*kernel_spectrum),
        .native = std::move(*native),
    };
}
core::Result<methods::ResolvedPsf> resolve_psf(const methods::Wiener& method) {
    auto side = methods::psf_side(method.parameters().psf);
    if (!side) {
        return std::unexpected(side.error());
    }
    if (*side == 0) {
        return core::failure(core::ErrorCode::argument, "File PSF requires host loading");
    }
    methods::ResolvedPsf psf;
    psf.width = *side;
    psf.height = *side;
    psf.coefficients.resize(std::size_t{*side} * (*side));
    const auto& requested = method.parameters().psf;
    auto generated =
        std::holds_alternative<methods::GaussianPsf>(requested)
            ? methods::gaussian_psf(std::get<methods::GaussianPsf>(requested), psf.coefficients)
            : methods::motion_psf(std::get<methods::MotionPsf>(requested), psf.coefficients);
    if (!generated) {
        return std::unexpected(generated.error());
    }
    auto centroid = methods::psf_centroid(psf.width, psf.height, psf.coefficients);
    if (!centroid) {
        return std::unexpected(centroid.error());
    }
    psf.centroid_x = centroid->front();
    psf.centroid_y = centroid->back();
    return psf;
}
} // namespace docenhance::opencv
