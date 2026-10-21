// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "illumination_reference.hpp"
#include "require.hpp"
#include "surface_reference.hpp"
#include "surface_working_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace docenhance::tests {
struct SurfaceWorkingReference {
    LogReference field;
    std::vector<double> weights;
    std::vector<double> logarithms;
    std::optional<double> background_reference = std::nullopt;
};
inline std::vector<double> surface_cell_values(SurfaceWorkingFixture& fixture, ReferenceCell cell) {
    std::vector<double> values;
    values.reserve(std::size_t{cell.right - cell.left} * (cell.bottom - cell.top));
    for (auto y = cell.top; y < cell.bottom; ++y) {
        const auto source = fixture.luminance.view().row(y);
        const auto mask = fixture.protection.view().row(y);
        for (auto x = cell.left; x < cell.right; ++x) {
            if (mask.subspan(x, 1).front() == 0) {
                values.push_back(source.subspan(x, 1).front());
            }
        }
    }
    return values;
}
inline void surface_reference_cells(SurfaceWorkingFixture& fixture, methods::SurfaceParameters p,
                                    SurfaceWorkingReference& reference) {
    const auto cell = reference.field.cell;
    const auto columns = (fixture.luminance.width() + cell - 1) / cell;
    const auto rows = (fixture.luminance.height() + cell - 1) / cell;
    reference.weights.resize(std::size_t{columns} * rows);
    reference.logarithms.resize(reference.weights.size());
    for (std::uint32_t index = 0; index < columns * rows; ++index) {
        const auto left = (index % columns) * cell;
        const auto top = (index / columns) * cell;
        const ReferenceCell region{
            .left = left,
            .top = top,
            .right = std::min(left + cell, fixture.luminance.width()),
            .bottom = std::min(top + cell, fixture.luminance.height()),
        };
        const auto values = surface_cell_values(fixture, region);
        const auto area = (region.right - left) * (region.bottom - top);
        const auto required = std::max(std::min(16U, area), (area + 3) / 4);
        if (values.size() < required) {
            continue;
        }
        const auto quantile = sorted_rank(values, p.quantile);
        if (quantile >= 0.02) {
            reference.weights.at(index) = static_cast<double>(values.size()) / area;
            reference.logarithms.at(index) = std::log(quantile);
        }
    }
}
inline void surface_reference_dark(SurfaceWorkingReference& reference) {
    std::vector<double> selected;
    selected.reserve(reference.weights.size());
    for (std::size_t i = 0; i < reference.weights.size(); ++i) {
        if (reference.weights.at(i) > 0) {
            selected.push_back(reference.logarithms.at(i));
        }
    }
    require(!selected.empty(), "independent active reference has real measured cells");
    const auto logarithm = sorted_rank(selected, 0.9);
    reference.background_reference = std::exp(logarithm);
    const auto floor = logarithm - std::log(4.0);
    for (std::size_t i = 0; i < reference.weights.size(); ++i) {
        if (reference.weights.at(i) == 0) {
            continue;
        }
        if (reference.logarithms.at(i) < floor) {
            reference.weights.at(i) = 0;
            reference.logarithms.at(i) = 0;
            ++reference.field.dark_cells;
        } else {
            ++reference.field.measured_cells;
        }
    }
}
inline SurfaceWorkingReference surface_working_reference(SurfaceWorkingFixture& fixture,
                                                         methods::SurfaceParameters p) {
    const auto automatic = static_cast<std::uint32_t>(
        std::floor((std::min(fixture.luminance.width(), fixture.luminance.height()) / 24.0) + 0.5));
    const auto cell = p.cell.value_or(std::clamp(automatic, 16U, 256U));
    SurfaceWorkingReference reference{
        .field =
            {
                .extent =
                    {
                        .width = fixture.luminance.width(),
                        .height = fixture.luminance.height(),
                    },
                .cell = cell,
                .values = {},
            },
        .weights = {},
        .logarithms = {},
    };
    surface_reference_cells(fixture, p, reference);
    if (std::ranges::none_of(reference.weights, [](double weight) { return weight > 0; })) {
        return reference;
    }
    surface_reference_dark(reference);
    double sum_weights = 0;
    double sum_values = 0;
    std::optional<double> constant;
    bool same = true;
    for (std::size_t i = 0; i < reference.weights.size(); ++i) {
        const auto weight = reference.weights.at(i);
        const auto value = reference.logarithms.at(i);
        sum_weights += weight;
        sum_values += weight * value;
        if (weight > 0) {
            if (!constant) {
                constant = value;
            } else {
                same = same && *constant == value;
            }
        }
    }
    if (same) {
        // The specified weighted-mean initial state already meets the residual certificate;
        // use the analytic constant state, retaining its actual ordered binary64 mean.
        reference.field.values.assign(reference.weights.size(), sum_values / sum_weights);
    } else {
        reference.field.values = dense_surface_solution({
            .columns = (fixture.luminance.width() + cell - 1) / cell,
            .rows = (fixture.luminance.height() + cell - 1) / cell,
            .weights = reference.weights,
            .measured = reference.logarithms,
            .smooth = p.smooth,
        });
    }
    require(!reference.field.values.empty(), "independent edge-assembled dense solve succeeds");
    return reference;
}
inline double surface_reference_background(const SurfaceWorkingReference& reference,
                                           std::uint32_t x, std::uint32_t y) {
    if (std::ranges::all_of(reference.field.values, [&](double value) {
            return value == reference.field.values.front();
        })) {
        return std::exp(reference.field.values.front());
    }
    return reference.field.background(x, y);
}
inline std::vector<image::Coordinate> surface_reference_points(SurfaceWorkingFixture& fixture,
                                                               std::uint32_t stride) {
    std::vector<image::Coordinate> points;
    points.reserve(std::size_t{fixture.luminance.width()} * fixture.luminance.height());
    for (std::uint32_t y = 0; y < fixture.luminance.height(); y += stride) {
        const auto mask = fixture.protection.view().row(y);
        for (std::uint32_t x = 0; x < fixture.luminance.width(); x += stride) {
            if (mask.subspan(x, 1).front() == 0) {
                points.push_back({.x = x, .y = y});
            }
        }
    }
    return points;
}
inline methods::SurfaceMeasurements
surface_reference_measurements(SurfaceWorkingFixture& fixture,
                               const SurfaceWorkingReference& reference,
                               methods::SurfaceParameters p) {
    const auto stride =
        (std::max(fixture.luminance.width(), fixture.luminance.height()) + 1023) / 1024;
    auto points = surface_reference_points(fixture, stride);
    const bool fallback = points.empty();
    if (fallback) {
        points = surface_reference_points(fixture, 1);
    }
    require(!points.empty(), "independent selected population is nonempty");
    std::vector<double> y_values;
    std::vector<double> b_values;
    y_values.reserve(points.size());
    b_values.reserve(points.size());
    std::uint32_t paper = 0;
    std::uint32_t dark = 0;
    for (const auto point : points) {
        const auto value = fixture.luminance.view().row(point.y).subspan(point.x, 1).front();
        const auto b = surface_reference_background(reference, point.x, point.y);
        y_values.push_back(value);
        b_values.push_back(b);
        paper += static_cast<std::uint32_t>(value >= 0.8 * b);
        dark += static_cast<std::uint32_t>(value < 0.75 * b);
    }
    const auto low = sorted_rank(b_values, 0.1);
    const auto high = sorted_rank(b_values, 0.9);
    return {
        .stride = stride,
        .count = static_cast<std::uint32_t>(points.size()),
        .fallback = fallback,
        .target = p.target.value_or(high),
        .background_q10 = low,
        .background_q50 = sorted_rank(b_values, 0.5),
        .background_q90 = high,
        .luminance_q90 = sorted_rank(y_values, 0.9),
        .variation = (high - low) / std::max(high, 0.02),
        .paper_fraction = static_cast<double>(paper) / static_cast<double>(points.size()),
        .dark_fraction = static_cast<double>(dark) / static_cast<double>(points.size()),
    };
}
} // namespace docenhance::tests
