// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/working.hpp"
#include "sharpen_detail.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>

namespace docenhance::methods {
namespace {
core::Result<image::Plane<double>> allocate(Working working) {
    if (working.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    return image::Plane<double>::allocate(working.budget.get(), working.luminance.width(),
                                          working.luminance.height());
}
bool gather_row(Working working, image::PlaneView<double> field, std::uint32_t y) noexcept {
    const auto source = working.luminance.row(y);
    const auto destination = field.row(y);
    const auto cancellation = working.scheduler.get().cancellation();
    for (std::size_t x = 0; x < source.size(); ++x) {
        constexpr std::size_t interval = 1024;
        if (x % interval == 0 && cancellation.requested(core::Checkpoint::measurement)) {
            return false;
        }
        destination.subspan(x, 1).front() = image::srgb_encode(source.subspan(x, 1).front());
    }
    return true;
}
core::Result<void> gather(Working working, image::PlaneView<double> field) {
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = unsharp_rows(field.height(), band);
        for (auto y = first; y < last; ++y) {
            if (!gather_row(working, field, y)) {
                return core::cancelled();
            }
        }
        return {};
    };
    return working.scheduler.get().for_each(unsharp_band_count(field.height()),
                                            exec::WorkRef{work});
}
core::Result<void> gaussian(Working working, image::PlaneView<const double> input,
                            image::PlaneView<double> output, image::GaussianPass descriptor) {
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = unsharp_rows(input.height(), band);
        auto pass = descriptor;
        pass.first_row = first;
        pass.last_row = last;
        return image::gaussian_pass(input, output, pass, working.scheduler.get().cancellation());
    };
    return working.scheduler.get().for_each(unsharp_band_count(input.height()),
                                            exec::WorkRef{work});
}
} // namespace
core::Result<image::Plane<double>> fit_unsharp(const Unsharp& method, Working working) {
    auto field = allocate(working);
    if (!field) {
        return std::unexpected(std::move(field.error()));
    }
    auto work = allocate(working);
    if (!work) {
        return std::unexpected(std::move(work.error()));
    }
    if (auto gathered = gather(working, field->view()); !gathered) {
        return std::unexpected(std::move(gathered.error()));
    }
    std::array<double, unsharp_coefficients_max> coefficients{};
    auto pass = unsharp_gaussian(coefficients, method);
    pass.horizontal = true;
    if (auto horizontal = gaussian(working, field->view().as_const(), work->view(), pass);
        !horizontal) {
        return std::unexpected(std::move(horizontal.error()));
    }
    pass.horizontal = false;
    if (auto vertical = gaussian(working, work->view().as_const(), field->view(), pass);
        !vertical) {
        return std::unexpected(std::move(vertical.error()));
    }
    return std::move(*field);
}
} // namespace docenhance::methods
