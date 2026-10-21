// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/plane.hpp"
#include "fft_detail.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
namespace docenhance::fft {
bool valid_real(image::PlaneView<const double> view) noexcept {
    if (view.empty() || !valid_extent({.width = view.width(), .height = view.height()})) {
        return false;
    }
    const auto limit = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    const auto stride = view.shape().stride;
    const auto rows = std::size_t{view.height() - 1};
    const auto logical = std::size_t{view.width()} * sizeof(double);
    if (stride > limit || (rows != 0 && stride > limit / rows)) {
        return false;
    }
    if (rows * stride > limit - logical) {
        return false;
    }
    // The provider's iterator briefly holds the end-of-block row offset as an integer,
    // without dereferencing final padding. This is not a canonical-stride/backing demand.
    const auto block = std::min(block_size, view.height());
    return stride <= limit / block;
}
bool overlaps(std::span<const std::byte> a, std::span<const std::byte> b) noexcept {
    if (a.empty() || b.empty()) {
        return false;
    }
    const auto left = std::bit_cast<std::uintptr_t>(a.data());
    const auto right = std::bit_cast<std::uintptr_t>(b.data());
    return left <= right ? right - left < a.size() : left - right < b.size();
}
} // namespace docenhance::fft
