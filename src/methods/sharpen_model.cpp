// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
core::Result<void> count(image::Extent extent, image::PlaneView<const std::uint8_t> mask,
                         const SharpenExecution& e) {
    if (extent.width == 0 || extent.height == 0 ||
        (!mask.empty() && (mask.width() != extent.width || mask.height() != extent.height))) {
        return core::failure(core::ErrorCode::argument, "Invalid sharpening source or mask extent");
    }
    const auto total = std::uint64_t{extent.width} * extent.height;
    if (total > image::source_pixels_max) {
        return core::failure(core::ErrorCode::resource, "Sharpening pixel limit exceeded");
    }
    auto& r = e.report.get();
    for (std::uint32_t y = 0; y < mask.height(); ++y) {
        for (std::uint32_t x = 0; x < mask.width(); ++x) {
            constexpr std::uint32_t interval = 1024;
            if (x % interval == 0 && e.cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            r.protected_samples +=
                static_cast<std::uint64_t>(mask.row(y).subspan(x, 1).front() != 0);
        }
    }
    r.eligible_samples = total - r.protected_samples;
    return {};
}
core::Result<image::Plane<double>> allocate(std::uint32_t w, std::uint32_t h,
                                            const SharpenExecution& e) {
    if (e.cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto p = image::Plane<double>::allocate(e.budget.get(), w, h);
    if (p) {
        e.report.get().preparation_charge_peak =
            std::max(e.report.get().preparation_charge_peak,
                     static_cast<std::uint64_t>(e.budget.get().used()));
    }
    return p;
}
core::Result<void> append(image::RowRange position, std::span<const double> rgb,
                          image::PlaneView<double> field, const SharpenExecution& e) {
    for (std::uint32_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
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
        auto f = image::srgb_encode(*value);
        if (!f) {
            return std::unexpected(f.error());
        }
        field.row(position.row).subspan(position.first + i, 1).front() = *f;
        ++e.report.get().context_samples;
    }
    return {};
}
core::Result<void> gather(image::LinearSource& source, image::PlaneView<double> field,
                          std::span<double> transfer, const SharpenExecution& e) {
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
} // namespace
core::Result<SharpenModel> SharpenModel::prepare(image::LinearSource& source,
                                                 image::PlaneView<const std::uint8_t> mask,
                                                 const Unsharp& method, const SharpenExecution& e) {
    auto& r = e.report.get();
    r = {};
    r.requested = method.parameters();
    r.status = SharpenStatus::failed;
    if (e.cancellation.requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    auto counted = count(source.extent(), mask, e);
    if (!counted) {
        return std::unexpected(counted.error());
    }
    if (method.parameters().amount == 0 || r.eligible_samples == 0) {
        r.status = SharpenStatus::no_change;
        r.reason = method.parameters().amount == 0 ? SharpenReason::zero_amount
                                                   : SharpenReason::no_eligible_samples;
        r.complete = true;
        return SharpenModel{source.extent(), method, {}};
    }
    const auto extent = source.extent();
    auto field = allocate(extent.width, extent.height, e);
    if (!field) {
        return std::unexpected(field.error());
    }
    auto work = allocate(extent.width, extent.height, e);
    if (!work) {
        return std::unexpected(work.error());
    }
    auto transfer = allocate(image::linear_block_pixels * image::rgb_channels, 1, e);
    if (!transfer) {
        return std::unexpected(transfer.error());
    }
    auto measured = gather(source, field->view(), transfer->view().row(0), e);
    if (!measured) {
        return std::unexpected(measured.error());
    }
    constexpr std::uint32_t maximum_radius = 9;
    std::array<double, (2 * maximum_radius) + 1> coefficients{};
    const auto sigma = method.parameters().sigma;
    const auto radius = static_cast<std::uint32_t>(std::ceil(3 * sigma));
    const auto weights = std::span<double>{coefficients}.first((2 * radius) + 1);
    double normalization = 0;
    for (std::uint32_t k = 0; k < weights.size(); ++k) {
        const auto offset = static_cast<double>(k) - radius;
        weights.subspan(k, 1).front() = std::exp(-(offset * offset) / (2 * sigma * sigma));
        normalization += weights.subspan(k, 1).front();
    }
    auto horizontal = image::gaussian_pass(
        field->view().as_const(), work->view(),
        {.weights = weights, .normalization = normalization, .horizontal = true}, e.cancellation);
    if (!horizontal) {
        return std::unexpected(horizontal.error());
    }
    auto vertical = image::gaussian_pass(
        work->view().as_const(), field->view(),
        {.weights = weights, .normalization = normalization, .horizontal = false}, e.cancellation);
    if (!vertical) {
        return std::unexpected(vertical.error());
    }
    return SharpenModel{extent, method, std::move(*field)};
}
} // namespace docenhance::methods
