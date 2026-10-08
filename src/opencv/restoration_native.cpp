// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "diagnostics.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/opencv/restoration.hpp"
#include "restoration_work.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <new>
#include <opencv2/core.hpp>
namespace docenhance::opencv {
namespace {
core::Result<void> transform(image::PlaneView<float> input, image::PlaneView<float> output,
                             bool inverse, const RestorationExecution& e) {
    if (e.cancellation.requested(core::Checkpoint::solving)) {
        return core::cancelled();
    }
    try {
        configure_diagnostics();
        const auto width = inverse ? input.width() / 2 : input.width();
        const cv::Mat source(static_cast<int>(input.height()), static_cast<int>(width),
                             inverse ? CV_32FC2 : CV_32FC1, input.row(0).data(),
                             input.shape().stride);
        cv::Mat destination(static_cast<int>(output.height()), static_cast<int>(width),
                            inverse ? CV_32FC1 : CV_32FC2, output.row(0).data(),
                            output.shape().stride);
        const auto* const before = destination.data;
        constexpr int inverse_flags = cv::DFT_INVERSE + cv::DFT_REAL_OUTPUT + cv::DFT_SCALE;
        const auto flags = inverse ? inverse_flags : cv::DFT_COMPLEX_OUTPUT;
        cv::dft(source, destination, flags);
        ++e.report.get().native_calls;
        if (destination.data != before) {
            return core::failure(core::ErrorCode::invariant, "Native DFT replaced borrowed output");
        }
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource, "Native DFT allocation failed");
    } catch (const cv::Exception& error) {
        return core::failure(error.code == cv::Error::StsNoMem ? core::ErrorCode::resource
                                                               : core::ErrorCode::numerical,
                             "Native DFT execution failed");
    } catch (...) {
        return core::failure(core::ErrorCode::invariant, "Unexpected native DFT failure");
    }
    if (e.cancellation.requested(core::Checkpoint::solving)) {
        return core::cancelled();
    }
    return {};
}
core::Result<void> filter(RestorationWork& work, const methods::Wiener& method,
                          const RestorationExecution& e) {
    for (std::uint32_t y = 0; y < work.spectrum.height(); ++y) {
        auto const f = work.spectrum.view().row(y);
        const auto h = work.kernel_spectrum.view().row(y);
        for (std::uint32_t x = 0; x < work.input.width(); ++x) {
            constexpr std::uint32_t interval = 1024;
            if (x % interval == 0 && e.cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            const auto fx = f.subspan(std::size_t{x} * 2, 2);
            const auto hx = h.subspan(std::size_t{x} * 2, 2);
            const auto fr = static_cast<double>(fx.front());
            const auto fi = static_cast<double>(fx.back());
            const auto hr = static_cast<double>(hx.front());
            const auto hi = static_cast<double>(hx.back());
            const auto divisor = (hr * hr) + (hi * hi) + method.parameters().k;
            const auto ur = ((hr * fr) + (hi * fi)) / divisor;
            const auto ui = ((hr * fi) - (hi * fr)) / divisor;
            if (!std::isfinite(ur) || !std::isfinite(ui)) {
                return core::failure(core::ErrorCode::numerical, "Nonfinite restoration spectrum");
            }
            fx.front() = static_cast<float>(ur);
            fx.back() = static_cast<float>(ui);
        }
    }
    return {};
}
core::Result<void> crop(RestorationWork& work, const RestorationExecution& e) {
    const auto guard = e.report.get().guard;
    if (!e.report.get().padded_mean) {
        return core::failure(core::ErrorCode::invariant, "Missing restoration padded mean");
    }
    const auto mean = *e.report.get().padded_mean;
    for (std::uint32_t y = 0; y < work.candidate.height(); ++y) {
        for (std::uint32_t x = 0; x < work.candidate.width(); ++x) {
            constexpr std::uint32_t interval = 1024;
            if (x % interval == 0 && e.cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            const auto candidate =
                mean +
                static_cast<double>(work.input.view().row(y + guard).subspan(x + guard, 1).front());
            if (!std::isfinite(candidate)) {
                return core::failure(core::ErrorCode::numerical, "Nonfinite restoration candidate");
            }
            work.candidate.view().row(y).subspan(x, 1).front() = candidate;
        }
    }
    return {};
}
} // namespace
core::Result<void> restoration_transforms(RestorationWork& work, const methods::Wiener& method,
                                          const RestorationExecution& e) {
    auto forward = transform(work.input.view(), work.spectrum.view(), false, e);
    if (!forward) {
        return std::unexpected(forward.error());
    }
    auto kernel = transform(work.kernel.view(), work.kernel_spectrum.view(), false, e);
    if (!kernel) {
        return std::unexpected(kernel.error());
    }
    auto filtered = filter(work, method, e);
    if (!filtered) {
        return std::unexpected(filtered.error());
    }
    auto inverse = transform(work.spectrum.view(), work.input.view(), true, e);
    if (!inverse) {
        return std::unexpected(inverse.error());
    }
    return crop(work, e);
}
} // namespace docenhance::opencv
