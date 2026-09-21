// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Property fuzzing of the numeric reference primitives against exact, independent references:
// wide multiplication for raster budgets, a slow reflection loop, sorting for percentiles, and
// the documented sRGB/luminance tolerances.
#include "docenhance/image/numeric.hpp"

#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>
namespace {
using docenhance::fuzz::FuzzInput;
using docenhance::fuzz::require;
namespace image = docenhance::image;
constexpr std::size_t max_samples = 64;
constexpr std::int64_t slow_reference_span = 4096;

struct Wide {
    std::uint64_t high;
    std::uint64_t low;
};
// Exact 64x64 -> 128-bit product from 32-bit limbs; portable to compilers without __int128.
Wide multiply(std::uint64_t a, std::uint64_t b) {
    constexpr std::uint64_t mask = 0xFFFF'FFFFU;
    const std::uint64_t ll = (a & mask) * (b & mask);
    const std::uint64_t lh = (a & mask) * (b >> 32U);
    const std::uint64_t hl = (a >> 32U) * (b & mask);
    const std::uint64_t hh = (a >> 32U) * (b >> 32U);
    const std::uint64_t middle = (ll >> 32U) + (lh & mask) + (hl & mask);
    return {
        .high = hh + (lh >> 32U) + (hl >> 32U) + (middle >> 32U),
        .low = (middle << 32U) | (ll & mask),
    };
}

std::uint64_t sample(FuzzInput& input) {
    // Mostly small dimensions, sometimes arbitrary ones, so both paths are reached.
    return (input.byte() & 1U) == 0 ? input.bounded(0xFFFF) : input.integer<std::uint64_t>();
}

void raster_budget(FuzzInput& input) {
    const auto width = sample(input);
    const auto height = sample(input);
    const auto channels = sample(input);
    const auto limit = sample(input);
    const auto result = docenhance::image::checked_elements(width, height, channels, limit);
    const auto pixels = multiply(width, height);
    const auto elements = multiply(pixels.low, channels);
    const bool fits = width != 0 && height != 0 && channels != 0 && pixels.high == 0 &&
                      elements.high == 0 && elements.low <= limit;
    require(result.has_value() == fits, "checked_elements accepts exactly budgets that fit");
    require(!fits || result.value() == elements.low, "checked_elements returns the exact product");
}

void reflection(FuzzInput& input) {
    const auto coordinate = static_cast<std::int64_t>(input.integer<std::uint64_t>());
    const auto extent =
        (input.byte() & 1U) == 0 ? input.bounded(64) : input.integer<std::uint64_t>();
    const auto result = image::reflect101(coordinate, extent);
    constexpr auto max_extent =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 2) + 1;
    require(result.has_value() == (extent != 0 && extent <= max_extent),
            "reflect101 accepts exactly the supported extents");
    if (!result.has_value()) {
        return;
    }
    require(result.value() < extent, "reflection stays inside the extent");
    if (coordinate != std::numeric_limits<std::int64_t>::min()) {
        require(image::reflect101(-coordinate, extent).value() == result.value(),
                "reflect-101 is symmetric about zero");
    }
    // A range check, not std::abs: |INT64_MIN| overflows.
    const bool near_zero = coordinate >= -slow_reference_span && coordinate <= slow_reference_span;
    if (extent > 1 && extent <= 64 && near_zero) {
        const auto last = static_cast<std::int64_t>(extent) - 1;
        std::int64_t slow = coordinate;
        while (slow < 0 || slow > last) {
            slow = slow < 0 ? -slow : (2 * last) - slow;
        }
        require(std::cmp_equal(result.value(), slow), "matches the slow reference");
    }
}

bool unit_interval(double value) {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

void transfer(FuzzInput& input) {
    const auto x = (input.byte() & 1U) == 0 ? input.unit() : input.any_double();
    const auto y = input.unit();
    const auto decoded = image::srgb_decode(x);
    require(decoded.has_value() == unit_interval(x), "srgb_decode accepts exactly [0,1]");
    require(image::srgb_encode(x).has_value() == unit_interval(x),
            "srgb_encode accepts exactly [0,1]");
    if (!decoded.has_value()) {
        return;
    }
    require(unit_interval(decoded.value()), "decoding stays in [0,1]");
    require(std::abs(image::srgb_encode(decoded.value()).value() - x) < 3e-8, "sRGB round trip");
    const auto [low, high] = std::minmax(x, y);
    require(image::srgb_decode(low).value() <= image::srgb_decode(high).value(),
            "decode is monotone");
    require(image::srgb_encode(low).value() <= image::srgb_encode(high).value(),
            "encode is monotone");
}

void luminance(FuzzInput& input) {
    const bool arbitrary = (input.byte() & 1U) != 0;
    image::Rgb rgb{};
    for (auto& channel : rgb) {
        channel = arbitrary ? input.any_double() : input.unit();
    }
    const auto target = arbitrary ? input.any_double() : input.unit();
    const auto result = image::transport_luminance(rgb, target);
    const bool valid = std::ranges::all_of(rgb, unit_interval) && unit_interval(target);
    require(result.has_value() == valid, "transport accepts exactly unit RGB and target");
    if (!valid) {
        return;
    }
    require(std::ranges::all_of(result.value(), unit_interval), "transport stays in gamut");
    require(std::abs(image::luminance(result.value()).value() - target) <= 1e-12,
            "transport reaches the target luminance");
}

void percentile(FuzzInput& input) {
    const bool arbitrary = (input.byte() & 1U) != 0;
    std::vector<double> values(static_cast<std::size_t>(input.bounded(max_samples)));
    for (auto& value : values) {
        value = arbitrary ? input.any_double() : static_cast<double>(input.bounded(16));
    }
    const auto p = arbitrary ? input.any_double() : input.unit();
    const auto result = image::nearest_rank(values, p);
    const bool valid = !values.empty() && unit_interval(p) &&
                       std::ranges::all_of(values, [](double v) { return std::isfinite(v); });
    require(result.has_value() == valid,
            "nearest_rank accepts exactly finite samples and p in [0,1]");
    if (!valid) {
        return;
    }
    std::ranges::sort(values);
    const auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(values.size())));
    const auto index = rank == 0 ? 0 : std::min(rank - 1, values.size() - 1);
    require(result.value() == values.at(index), "nearest rank equals the sorted reference");
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    FuzzInput input(std::span<const std::uint8_t>(data, size));
    switch (input.byte() % 5U) {
    case 0:
        raster_budget(input);
        break;
    case 1:
        reflection(input);
        break;
    case 2:
        transfer(input);
        break;
    case 3:
        luminance(input);
        break;
    default:
        percentile(input);
        break;
    }
    return 0;
}
