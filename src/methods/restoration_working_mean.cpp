// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/working.hpp"
#include "restoration_working_detail.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
struct RestorationSum {
    double sum = 0;
    double correction = 0;
    void add(double value) noexcept {
        const auto adjusted = value - correction;
        const auto next = sum + adjusted;
        correction = (next - sum) - adjusted;
        sum = next;
    }
};
core::Result<void> pad_row(Working working, image::PlaneView<double> frame,
                           RestorationGeometry geometry, std::span<double> sums, std::uint32_t y) {
    const auto source_y = image::reflect101_folded(static_cast<std::int64_t>(y) - geometry.guard,
                                                   working.luminance.height());
    const auto source = working.luminance.row(static_cast<std::uint32_t>(source_y));
    const auto target = frame.row(y);
    const auto cancellation = working.scheduler.get().cancellation();
    RestorationSum sum;
    for (std::uint32_t x = 0; x < target.size(); ++x) {
        if (x % restoration_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        const auto source_x = image::reflect101_folded(
            static_cast<std::int64_t>(x) - geometry.guard, working.luminance.width());
        const auto value = source.subspan(static_cast<std::size_t>(source_x), 1).front();
        if (!std::isfinite(value) || value < 0 || value > 1) {
            return core::failure(core::ErrorCode::argument, "Invalid restoration working sample");
        }
        target.subspan(x, 1).front() = value;
        sum.add(value);
    }
    sums.subspan(y, 1).front() = sum.sum;
    return {};
}
core::Result<void> center_row(std::span<double> row, double mean,
                              const core::Cancellation& cancellation) {
    for (std::size_t x = 0; x < row.size(); ++x) {
        if (x % restoration_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        row.subspan(x, 1).front() -= mean;
    }
    return {};
}
} // namespace
core::Result<double> restoration_mean(Working working, image::PlaneView<double> frame,
                                      RestorationGeometry geometry) {
    const auto cancellation = working.scheduler.get().cancellation();
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto sums = image::Plane<double>::allocate(working.budget.get(), frame.height(), 1);
    if (!sums) {
        return std::unexpected(std::move(sums.error()));
    }
    const auto values = sums->view().row(0);
    const auto task = [&](std::size_t index) -> core::Result<void> {
        const auto band = restoration_band(frame.height(), index);
        for (auto y = band.first; y < band.first + band.rows; ++y) {
            if (auto row = pad_row(working, frame, geometry, values, y); !row) {
                return row;
            }
        }
        return {};
    };
    if (auto padded = working.scheduler.get().for_each(restoration_bands(frame.height()),
                                                       exec::WorkRef{task});
        !padded) {
        return std::unexpected(std::move(padded.error()));
    }
    RestorationSum total;
    for (std::uint32_t y = 0; y < frame.height(); ++y) {
        if (y % restoration_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        total.add(values.subspan(y, 1).front());
    }
    const auto mean =
        total.sum / static_cast<double>(std::uint64_t{frame.width()} * frame.height());
    if (!std::isfinite(mean) || mean < 0 || mean > 1) {
        return core::failure(core::ErrorCode::numerical, "Invalid restoration padded mean");
    }
    return mean;
}
core::Result<void> restoration_center(Working working, image::PlaneView<double> frame,
                                      double mean) {
    const auto task = [&](std::size_t index) -> core::Result<void> {
        const auto band = restoration_band(frame.height(), index);
        const auto cancellation = working.scheduler.get().cancellation();
        for (auto y = band.first; y < band.first + band.rows; ++y) {
            if (auto row = center_row(frame.row(y), mean, cancellation); !row) {
                return row;
            }
        }
        return {};
    };
    return working.scheduler.get().for_each(restoration_bands(frame.height()), exec::WorkRef{task});
}
} // namespace docenhance::methods
