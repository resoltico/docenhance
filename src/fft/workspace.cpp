// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/geometry.hpp"
#include "fft_detail.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
namespace docenhance::fft {
namespace {
constexpr std::array primes{2U, 3U, 5U};
constexpr std::size_t factor_slots = 64;
constexpr std::size_t factor_words = 3;
constexpr std::size_t metadata_words = 64;
constexpr std::size_t plan_words = 32;
constexpr std::size_t simultaneous_arrays = 3;
// Source-derived candidate, to be earned by actual production-TU C/C++ observations.
constexpr std::size_t control_bytes =
    (((factor_slots * factor_words) + metadata_words + plan_words) * sizeof(std::size_t)) +
    (simultaneous_arrays * core::buffer_alignment);
static_assert(sizeof(std::size_t) == sizeof(std::uint64_t));
bool smooth(std::uint32_t n) noexcept {
    if (n == 0) {
        return false;
    }
    for (const auto p : primes) {
        while (n % p == 0) {
            n /= p;
        }
    }
    return n == 1;
}
std::size_t twiddles(std::uint32_t value, bool real) noexcept {
    const auto n = std::size_t{value};
    std::size_t deductions = 0;
    auto const take = [&](std::uint32_t radix) {
        while (value % radix == 0) {
            deductions += radix - 1;
            value /= radix;
        }
    };
    constexpr std::uint32_t radix_eight = 8;
    constexpr std::uint32_t radix_four = 4;
    if (!real) {
        take(radix_eight);
    }
    take(radix_four);
    for (const auto p : primes) {
        take(p);
    }
    return n - 1 - deductions;
}
core::Result<std::size_t> call_bytes(std::uint32_t n, std::uint32_t others, bool real) noexcept {
    const auto lanes = others >= native_lanes() ? native_lanes() : 1;
    const auto element = real ? sizeof(double) : sizeof(Complex);
    const auto half = (std::size_t{n} + 2) / 2;
    std::size_t q = 2;
    while (q * q < half) {
        q *= 2;
    }
    const auto constructor = sizeof(Complex) * (q + ((half + q - 1) / q));
    const auto execution = 2 * std::size_t{n} * lanes * element;
    const auto total =
        (element * twiddles(n, real)) + std::max(constructor, execution) + control_bytes;
    return total;
}
} // namespace
bool valid_extent(image::Extent extent) noexcept {
    return smooth(extent.width) && smooth(extent.height);
}
core::Result<std::size_t> phase_bytes(image::Extent extent, unsigned workers, Axis axis) noexcept {
    if (!valid_extent(extent) || workers == 0 || workers > exec::max_workers) {
        return failed(core::ErrorCode::argument);
    }
    const auto n = axis == Axis::rows ? extent.width : extent.height;
    const auto others = axis == Axis::rows ? extent.height : (extent.width / 2) + 1;
    const auto full = others / block_size;
    const auto tail = others % block_size;
    const auto capacity = call_bytes(n, block_size, axis == Axis::rows);
    if (!capacity) {
        return failed(capacity.error().code);
    }
    const auto active = std::min(std::size_t{workers}, std::size_t{full});
    if (*capacity > std::numeric_limits<std::size_t>::max() / workers) {
        return failed(core::ErrorCode::resource);
    }
    auto total = active * (*capacity);
    if (workers > full && tail != 0) {
        const auto last = call_bytes(n, tail, axis == Axis::rows);
        if (!last || *last > std::numeric_limits<std::size_t>::max() - total) {
            return failed(core::ErrorCode::resource);
        }
        total += *last;
    }
    return total;
}
core::Result<std::size_t> workspace_bytes(image::Extent extent, unsigned workers) noexcept {
    const auto rows = phase_bytes(extent, workers, Axis::rows);
    const auto columns = phase_bytes(extent, workers, Axis::columns);
    if (!rows) {
        return failed(rows.error().code);
    }
    if (!columns) {
        return failed(columns.error().code);
    }
    return std::max(*rows, *columns);
}
} // namespace docenhance::fft
