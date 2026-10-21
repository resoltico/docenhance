// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/methods/restoration.hpp"

#include <algorithm>
#include <cstdint>
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
    constexpr std::uint64_t maximum = restoration_fft_maximum;
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
} // namespace docenhance::methods
