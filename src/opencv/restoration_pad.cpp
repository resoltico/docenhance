// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
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
#include <span>
namespace docenhance::opencv {
namespace {
core::Result<void> append(image::RowRange position, std::span<const double> rgb,
                          image::PlaneView<double> field, const RestorationExecution& e) {
    const auto n = rgb.size() / image::rgb_channels;
    for (std::uint32_t i = 0; i < n; ++i) {
        constexpr std::uint32_t interval = 128;
        if (i % interval == 0 && e.cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        const auto pixel = rgb.subspan(std::size_t{i} * image::rgb_channels, image::rgb_channels);
        auto value = image::luminance(
            {pixel.front(), pixel.subspan(1, 1).front(), pixel.subspan(2, 1).front()});
        if (!value) {
            return std::unexpected(value.error());
        }
        field.row(position.row).subspan(position.first + i, 1).front() = *value;
        ++e.report.get().context_samples;
    }
    return {};
}
core::Result<double> padded_mean(const RestorationWork& work, const RestorationExecution& e) {
    const auto original = work.candidate.view();
    const auto padded = work.input.view();
    const auto guard = e.report.get().guard;
    double sum = 0;
    double correction = 0;
    for (std::uint32_t y = 0; y < padded.height(); ++y) {
        const auto sy = static_cast<std::uint32_t>(
            image::reflect101_folded(static_cast<std::int64_t>(y) - guard, original.height()));
        for (std::uint32_t x = 0; x < padded.width(); ++x) {
            constexpr std::uint32_t interval = 1024;
            if (x % interval == 0 && e.cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            const auto sx =
                image::reflect101_folded(static_cast<std::int64_t>(x) - guard, original.width());
            const auto value = original.row(sy).subspan(sx, 1).front();
            const auto adjusted = value - correction;
            const auto next = sum + adjusted;
            correction = (next - sum) - adjusted;
            sum = next;
        }
    }
    const auto mean = sum / static_cast<double>(std::uint64_t{padded.width()} * padded.height());
    return mean;
}
} // namespace
core::Result<void> restoration_gather(image::LinearSource& source, RestorationWork& work,
                                      const RestorationExecution& e) {
    auto const field = work.candidate.view();
    auto const transfer = work.transfer.view().row(0);
    for (std::uint32_t y = 0; y < field.height(); ++y) {
        for (std::uint32_t x = 0; x < field.width();) {
            if (e.cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            const auto n = std::min(image::linear_block_pixels, field.width() - x);
            const auto rgb = transfer.first(std::size_t{n} * image::rgb_channels);
            auto read = source.read({.row = y, .first = x}, rgb, e.preparation_use);
            if (!read) {
                return std::unexpected(read.error());
            }
            auto appended = append({.row = y, .first = x}, rgb, field, e);
            if (!appended) {
                return std::unexpected(appended.error());
            }
            x += n;
        }
    }
    return {};
}
core::Result<void> restoration_pad(RestorationWork& work, const methods::ResolvedPsf& psf,
                                   const RestorationExecution& e) {
    const auto original = work.candidate.view();
    auto const padded = work.input.view();
    const auto guard = e.report.get().guard;
    auto measured = padded_mean(work, e);
    if (!measured) {
        return std::unexpected(measured.error());
    }
    const auto mean = *measured;
    e.report.get().padded_mean = mean;
    for (std::uint32_t y = 0; y < padded.height(); ++y) {
        const auto sy = static_cast<std::uint32_t>(
            image::reflect101_folded(static_cast<std::int64_t>(y) - guard, original.height()));
        for (std::uint32_t x = 0; x < padded.width(); ++x) {
            constexpr std::uint32_t interval = 1024;
            if (x % interval == 0 && e.cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            const auto sx =
                image::reflect101_folded(static_cast<std::int64_t>(x) - guard, original.width());
            padded.row(y).subspan(x, 1).front() =
                static_cast<float>(original.row(sy).subspan(sx, 1).front() - mean);
            work.kernel.view().row(y).subspan(x, 1).front() = 0;
        }
    }
    for (std::uint32_t y = 0; y < psf.height; ++y) {
        if (e.cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto dy = (y + padded.height() - (psf.height / 2)) % padded.height();
        for (std::uint32_t x = 0; x < psf.width; ++x) {
            const auto dx = (x + padded.width() - (psf.width / 2)) % padded.width();
            work.kernel.view().row(dy).subspan(dx, 1).front() +=
                static_cast<float>(std::span<const double>{psf.coefficients}
                                       .subspan((std::size_t{y} * psf.width) + x, 1)
                                       .front());
        }
    }
    return {};
}
} // namespace docenhance::opencv
