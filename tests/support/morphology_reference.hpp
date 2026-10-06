// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace docenhance::tests {
inline std::uint32_t morphology_reflect(std::int64_t p, std::uint32_t n) {
    if (n == 1) {
        return 0;
    }
    while (p < 0 || p >= std::int64_t{n}) {
        p = p < 0 ? -p : (2 * std::int64_t{n}) - 2 - p;
    }
    return static_cast<std::uint32_t>(p);
}
struct MorphologyReferenceExtent {
    std::uint32_t width;
    std::uint32_t height;
};
struct MorphologyReferencePoint {
    std::uint32_t x;
    std::uint32_t y;
};
inline double square_sample(std::span<const double> input, MorphologyReferenceExtent extent,
                            MorphologyReferencePoint point, unsigned radius, bool dilation) {
    double value = dilation ? 0 : 1;
    const auto r = static_cast<std::int64_t>(radius);
    for (auto dy = -r; dy <= r; ++dy) {
        for (auto dx = -r; dx <= r; ++dx) {
            const auto xx = morphology_reflect(std::int64_t{point.x} + dx, extent.width);
            const auto yy = morphology_reflect(std::int64_t{point.y} + dy, extent.height);
            const auto sample = input[(std::size_t{yy} * extent.width) + xx];
            value = dilation ? std::max(value, sample) : std::min(value, sample);
        }
    }
    return value;
}
inline std::vector<double> square_reference(std::span<const double> input,
                                            MorphologyReferenceExtent extent, unsigned radius,
                                            bool dilation) {
    std::vector<double> out(input.size());
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            out[(std::size_t{y} * extent.width) + x] =
                square_sample(input, extent, {.x = x, .y = y}, radius, dilation);
        }
    }
    return out;
}
inline double gaussian_sample(std::span<const double> input, MorphologyReferenceExtent extent,
                              MorphologyReferencePoint point, unsigned radius) {
    const double sigma = std::max(0.5, static_cast<double>(radius) / 2);
    const auto r = static_cast<std::int64_t>(std::ceil(3 * sigma));
    double numerator = 0;
    double denominator = 0;
    for (auto dy = -r; dy <= r; ++dy) {
        for (auto dx = -r; dx <= r; ++dx) {
            const auto distance = static_cast<double>((dx * dx) + (dy * dy));
            const double weight = std::exp(-distance / (2 * sigma * sigma));
            const auto xx = morphology_reflect(std::int64_t{point.x} + dx, extent.width);
            const auto yy = morphology_reflect(std::int64_t{point.y} + dy, extent.height);
            numerator += input[(std::size_t{yy} * extent.width) + xx] * weight;
            denominator += weight;
        }
    }
    return numerator / denominator;
}
inline std::vector<double> gaussian_reference(std::span<const double> input,
                                              MorphologyReferenceExtent extent, unsigned radius) {
    std::vector<double> out(input.size());
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            out[(std::size_t{y} * extent.width) + x] =
                gaussian_sample(input, extent, {.x = x, .y = y}, radius);
        }
    }
    return out;
}
inline std::vector<double> morphology_reference(std::span<const double> input,
                                                MorphologyReferenceExtent extent, unsigned radius) {
    const auto dilated = square_reference(input, extent, radius, true);
    const auto closed = square_reference(dilated, extent, radius, false);
    return gaussian_reference(closed, extent, radius);
}
} // namespace docenhance::tests
