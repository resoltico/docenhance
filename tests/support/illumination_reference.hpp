// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "linear_fixture.hpp"
#include "surface_reference.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>
namespace docenhance::tests {
struct ReferenceAxis {
    std::uint32_t extent{}, cell{};
    [[nodiscard]] double center(std::uint32_t index) const noexcept {
        return (static_cast<double>(index * cell) + std::min((index + 1) * cell, extent) - 1) / 2;
    }
};
struct ReferenceInterval {
    std::uint32_t low{}, high{};
    double weight{};
};
inline ReferenceInterval reference_interval(ReferenceAxis axis, std::uint32_t position) {
    const auto count = (axis.extent + axis.cell - 1) / axis.cell;
    if (position <= axis.center(0)) {
        return {};
    }
    for (std::uint32_t i = 1; i < count; ++i) {
        if (position <= axis.center(i)) {
            return {
                .low = i - 1,
                .high = i,
                .weight = (position - axis.center(i - 1)) / (axis.center(i) - axis.center(i - 1)),
            };
        }
    }
    return {.low = count - 1, .high = count - 1};
}
struct LogReference {
    image::Extent extent;
    std::uint32_t cell{};
    std::vector<double> values;
    std::uint32_t measured_cells{};
    std::uint32_t dark_cells{};
    [[nodiscard]] double background(std::uint32_t x, std::uint32_t y) const {
        const auto columns = (extent.width + cell - 1) / cell;
        const auto a = reference_interval({.extent = extent.width, .cell = cell}, x);
        const auto b = reference_interval({.extent = extent.height, .cell = cell}, y);
        const auto sample = [&](std::uint32_t i, std::uint32_t j) {
            return values.at((std::size_t{j} * columns) + i);
        };
        const double top =
            ((1 - a.weight) * sample(a.low, b.low)) + (a.weight * sample(a.high, b.low));
        const double bottom =
            ((1 - a.weight) * sample(a.low, b.high)) + (a.weight * sample(a.high, b.high));
        return std::exp(((1 - b.weight) * top) + (b.weight * bottom));
    }
};
struct ReferenceCell {
    std::uint32_t left{}, top{}, right{}, bottom{};
};
inline std::vector<double> collect_cell(LinearFixture& source,
                                        image::PlaneView<const std::uint8_t> protection,
                                        ReferenceCell cell) {
    std::vector<double> values;
    for (auto y = cell.top; y < cell.bottom; ++y) {
        for (auto x = cell.left; x < cell.right; ++x) {
            if (protection.empty() || protection.row(y).subspan(x, 1).front() == 0) {
                values.push_back(
                    source.row(y).subspan(std::size_t{x} * image::rgb_channels, 1).front());
            }
        }
    }
    return values;
}
inline double sorted_rank(std::vector<double> values, double quantile) {
    std::ranges::sort(values);
    const auto rank =
        static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(values.size())));
    return values.at(rank == 0 ? 0 : rank - 1);
}
struct ReferenceCells {
    std::vector<double> weights;
    std::vector<double> quantiles; // Zero marks an unmeasured cell.
};
inline ReferenceCells measure_reference_cells(LinearFixture& source,
                                              image::PlaneView<const std::uint8_t> protection,
                                              std::uint32_t cell) {
    const auto extent = source.extent();
    const auto columns = (extent.width + cell - 1) / cell;
    const auto rows = (extent.height + cell - 1) / cell;
    ReferenceCells cells{
        .weights = std::vector<double>(std::size_t{columns} * rows),
        .quantiles = std::vector<double>(std::size_t{columns} * rows),
    };
    constexpr std::size_t minimum_samples = 16;
    constexpr double quantile = 0.9;
    constexpr double floor = 0.02;
    constexpr double coverage = 0.25;
    for (std::uint32_t at = 0; at < columns * rows; ++at) {
        const auto i = at % columns;
        const auto j = at / columns;
        const ReferenceCell region{
            .left = i * cell,
            .top = j * cell,
            .right = std::min((i + 1) * cell, extent.width),
            .bottom = std::min((j + 1) * cell, extent.height),
        };
        const auto eligible = collect_cell(source, protection, region);
        const auto area = std::size_t{region.right - region.left} * (region.bottom - region.top);
        const auto required =
            std::max(std::min(minimum_samples, area),
                     static_cast<std::size_t>(std::ceil(coverage * static_cast<double>(area))));
        if (eligible.size() < required) {
            continue;
        }
        const double q = sorted_rank(eligible, quantile);
        if (q >= floor) {
            cells.weights.at(at) = static_cast<double>(eligible.size()) / static_cast<double>(area);
            cells.quantiles.at(at) = q;
        }
    }
    return cells;
}
// The fitted field for the given smoothing, after the linear-domain dark-cell rule: a cell below
// a quarter of the measured cells' 90th-percentile quantile is left unmeasured.
inline LogReference illumination_reference(LinearFixture& source,
                                           image::PlaneView<const std::uint8_t> protection,
                                           std::uint32_t cell, double smooth) {
    const auto extent = source.extent();
    auto cells = measure_reference_cells(source, protection, cell);
    std::vector<double> measured;
    std::ranges::copy_if(cells.quantiles, std::back_inserter(measured),
                         [](double q) { return q > 0; });
    LogReference result{.extent = extent, .cell = cell, .values = {}};
    constexpr double reference_rank = 0.9;
    constexpr double gain_limit = 4;
    const double dark = measured.empty() ? 0 : sorted_rank(measured, reference_rank) / gain_limit;
    std::vector<double> values(cells.quantiles.size());
    for (std::size_t at = 0; at < values.size(); ++at) {
        const double q = cells.quantiles.at(at);
        if (q > 0 && q < dark) {
            cells.weights.at(at) = 0;
            ++result.dark_cells;
        } else if (q > 0) {
            values.at(at) = std::log(q);
            ++result.measured_cells;
        }
    }
    if (result.measured_cells != 0) {
        result.values = dense_surface_solution({
            .columns = (extent.width + cell - 1) / cell,
            .rows = (extent.height + cell - 1) / cell,
            .weights = cells.weights,
            .measured = values,
            .smooth = smooth,
        });
    }
    return result;
}
} // namespace docenhance::tests
