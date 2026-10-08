// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
namespace docenhance::methods {
namespace {
std::uint64_t optimal_size(std::uint64_t minimum, std::uint64_t maximum) {
    constexpr std::uint64_t ternary_factor = 3;
    constexpr std::uint64_t quinary_factor = 5;
    std::uint64_t best = maximum + 1;
    for (std::uint64_t p2 = 1; p2 <= maximum; p2 *= 2) {
        for (auto p3 = p2; p3 <= maximum; p3 *= ternary_factor) {
            auto candidate = p3;
            while (candidate < minimum) {
                candidate *= quinary_factor;
            }
            best = std::min(best, candidate);
        }
    }
    return best;
}
} // namespace
core::Result<std::uint32_t> restoration_fft_size(std::uint64_t minimum) {
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<int>::max() / 2);
    if (minimum == 0 || minimum > maximum) {
        return core::failure(core::ErrorCode::resource,
                             "Restoration FFT extent exceeds native domain");
    }
    const auto best = optimal_size(minimum, maximum);
    if (best > maximum) {
        return core::failure(core::ErrorCode::resource, "Restoration FFT extent unavailable");
    }
    return static_cast<std::uint32_t>(best);
}
core::Result<RestorationCharge> restoration_charge(image::Extent extent, image::Extent fft) {
    if (fft.width == 0 || fft.height == 0 ||
        fft.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 2)) {
        return core::failure(core::ErrorCode::resource, "Invalid restoration charge extent");
    }
    const auto bytes = [](std::uint32_t width, std::uint32_t height,
                          std::size_t sample) -> core::Result<std::uint64_t> {
        auto shape = image::plane_shape(width, height, sample);
        if (!shape) {
            return std::unexpected(shape.error());
        }
        auto value = image::plane_bytes(*shape);
        if (!value) {
            return std::unexpected(value.error());
        }
        return *value;
    };
    auto candidate = bytes(extent.width, extent.height, sizeof(double));
    auto transfer = bytes(image::linear_block_pixels * image::rgb_channels, 1, sizeof(double));
    auto real = bytes(fft.width, fft.height, sizeof(float));
    auto complex = bytes(2 * fft.width, fft.height, sizeof(float));
    if (!candidate) {
        return std::unexpected(candidate.error());
    }
    if (!transfer) {
        return std::unexpected(transfer.error());
    }
    if (!real) {
        return std::unexpected(real.error());
    }
    if (!complex) {
        return std::unexpected(complex.error());
    }
    constexpr auto control = restoration_native_control;
    const auto q = std::uint64_t{fft.width} * fft.height;
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (q > (maximum - control) / restoration_native_per_pixel) {
        return core::failure(core::ErrorCode::resource, "Restoration native charge overflows");
    }
    RestorationCharge charge{
        .candidate = *candidate,
        .transfer = *transfer,
        .real_plane = *real,
        .complex_plane = *complex,
        .native = (restoration_native_per_pixel * q) + control,
        .peak = 0,
    };
    for (const auto amount :
         {*candidate, *transfer, *real, *real, *complex, *complex, charge.native}) {
        if (amount > maximum - charge.peak) {
            return core::failure(core::ErrorCode::resource, "Restoration total charge overflows");
        }
        charge.peak += amount;
    }
    return charge;
}
} // namespace docenhance::methods
