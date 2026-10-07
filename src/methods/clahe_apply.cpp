// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast_detail.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/methods/contrast.hpp"

#include <algorithm>
#include <cstdint>
namespace docenhance::methods {
namespace {
double center(std::uint32_t index, std::uint32_t size, std::uint32_t divisions) {
    const auto left = std::uint64_t{index} * size / divisions;
    const auto right = std::uint64_t{index + 1} * size / divisions;
    return (static_cast<double>(left) + static_cast<double>(right) - 1) / 2;
}
struct Neighbors {
    std::uint32_t first;
    std::uint32_t second;
    double weight;
};
Neighbors neighbors(std::uint32_t coordinate, std::uint32_t size, std::uint32_t divisions) {
    if (coordinate <= center(0, size, divisions)) {
        return {.first = 0, .second = 0, .weight = 0};
    }
    for (std::uint32_t i = 1; i < divisions; ++i) {
        const auto right = center(i, size, divisions);
        if (coordinate < right) {
            const auto left = center(i - 1, size, divisions);
            return {
                .first = i - 1,
                .second = i,
                .weight = (static_cast<double>(coordinate) - left) / (right - left),
            };
        }
    }
    return {.first = divisions - 1, .second = divisions - 1, .weight = 0};
}
double contextual(double f, std::uint32_t index, const ClaheMaps& maps) {
    if (maps.identity.at(index)) {
        return f;
    }
    const auto knots = maps.knots.view().row(index);
    const auto scaled = f * clahe_bins;
    const auto bin = std::min(clahe_bins - 1, static_cast<std::uint32_t>(scaled));
    return knots.subspan(bin, 1).front() +
           ((scaled - bin) * (knots.subspan(bin + 1, 1).front() - knots.subspan(bin, 1).front()));
}
double interpolate(double first, double second, double weight) {
    return first == second ? first : first + (weight * (second - first));
}
} // namespace
double clahe_candidate(double f, image::RowRange position, image::Extent extent,
                       const Clahe& method, const ClaheMaps& maps) {
    const auto p = method.parameters();
    const auto horizontal = neighbors(position.first, extent.width, p.grid_columns);
    const auto vertical = neighbors(position.row, extent.height, p.grid_rows);
    const auto upper =
        interpolate(contextual(f, (vertical.first * p.grid_columns) + horizontal.first, maps),
                    contextual(f, (vertical.first * p.grid_columns) + horizontal.second, maps),
                    horizontal.weight);
    const auto lower =
        interpolate(contextual(f, (vertical.second * p.grid_columns) + horizontal.first, maps),
                    contextual(f, (vertical.second * p.grid_columns) + horizontal.second, maps),
                    horizontal.weight);
    return interpolate(upper, lower, vertical.weight);
}
} // namespace docenhance::methods
