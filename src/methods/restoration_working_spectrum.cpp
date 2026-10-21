// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/working.hpp"
#include "restoration_working_detail.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::methods {
namespace {
core::Result<void> clear_row(std::span<double> row, const core::Cancellation& cancellation) {
    for (std::size_t x = 0; x < row.size(); ++x) {
        if (x % restoration_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::initialization)) {
            return core::cancelled();
        }
        row.subspan(x, 1).front() = 0;
    }
    return {};
}
core::Result<void> filter_row(std::span<std::complex<double>> f,
                              std::span<const std::complex<double>> h, double k,
                              const core::Cancellation& cancellation) {
    for (std::size_t x = 0; x < f.size(); ++x) {
        if (x % restoration_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto input = f.subspan(x, 1).front();
        const auto psf = h.subspan(x, 1).front();
        const auto denominator = std::norm(psf) + k;
        if (!std::isfinite(input.real()) || !std::isfinite(input.imag()) ||
            !std::isfinite(psf.real()) || !std::isfinite(psf.imag()) ||
            !std::isfinite(denominator) || denominator <= 0) {
            return core::failure(core::ErrorCode::numerical, "Invalid restoration spectrum");
        }
        const auto value = std::conj(psf) * input / denominator;
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
            return core::failure(core::ErrorCode::numerical, "Nonfinite restoration filter");
        }
        f.subspan(x, 1).front() = value;
    }
    return {};
}
} // namespace
core::Result<void> restoration_kernel(Working working, image::PlaneView<double> frame,
                                      const ResolvedPsf& psf) {
    const auto cancellation = working.scheduler.get().cancellation();
    const auto clear = [&](std::size_t index) -> core::Result<void> {
        const auto band = restoration_band(frame.height(), index);
        for (auto y = band.first; y < band.first + band.rows; ++y) {
            if (auto row = clear_row(frame.row(y), cancellation); !row) {
                return row;
            }
        }
        return {};
    };
    if (auto cleared = working.scheduler.get().for_each(restoration_bands(frame.height()),
                                                        exec::WorkRef{clear});
        !cleared) {
        return cleared;
    }
    const auto coefficients = std::span{psf.coefficients};
    for (std::uint32_t y = 0; y < psf.height; ++y) {
        const auto target_y =
            (std::int64_t{y} - (psf.height / 2) + frame.height()) % frame.height();
        for (std::uint32_t x = 0; x < psf.width; ++x) {
            if (x % restoration_checkpoint_samples == 0 &&
                cancellation.requested(core::Checkpoint::initialization)) {
                return core::cancelled();
            }
            const auto target_x =
                (std::int64_t{x} - (psf.width / 2) + frame.width()) % frame.width();
            frame.row(static_cast<std::uint32_t>(target_y))
                .subspan(static_cast<std::size_t>(target_x), 1)
                .front() = coefficients.subspan((std::size_t{y} * psf.width) + x, 1).front();
        }
    }
    return {};
}
core::Result<void> restoration_filter(std::span<std::complex<double>> signal,
                                      std::span<const std::complex<double>> kernel,
                                      image::PlaneShape shape, const Wiener& method,
                                      const exec::Scheduler& scheduler) {
    const auto pitch = shape.stride / sizeof(std::complex<double>);
    const auto task = [&](std::size_t index) -> core::Result<void> {
        const auto band = restoration_band(shape.height, index);
        const auto cancellation = scheduler.cancellation();
        for (auto y = band.first; y < band.first + band.rows; ++y) {
            const auto f = signal.subspan(std::size_t{y} * pitch, shape.width);
            const auto h = kernel.subspan(std::size_t{y} * pitch, shape.width);
            if (auto row = filter_row(f, h, method.parameters().k, cancellation); !row) {
                return row;
            }
        }
        return {};
    };
    return scheduler.for_each(restoration_bands(shape.height), exec::WorkRef{task});
}
} // namespace docenhance::methods
