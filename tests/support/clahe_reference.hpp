// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "clahe_fixture.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/clahe.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "require.hpp"
#include "sharpen_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace docenhance::tests {
struct ClaheReferenceMap {
    std::array<double, 1025> knots{};
    bool identity = true;
};
inline ClaheReferenceMap clahe_reference_fit(const std::vector<double>& samples, double clip) {
    ClaheReferenceMap map;
    if (samples.size() < 16 ||
        *std::ranges::max_element(samples) - *std::ranges::min_element(samples) < 1.0 / 4096) {
        return map;
    }
    map.identity = false;
    std::array<std::uint64_t, 1024> histogram{};
    for (const auto f : samples) {
        const auto bin =
            std::min(std::size_t{1023}, static_cast<std::size_t>(std::floor(f * 1024)));
        ++histogram.at(bin);
    }
    const auto limit = static_cast<std::uint64_t>(
        std::max(1.0, std::floor(clip * static_cast<double>(samples.size()) / 1024)));
    std::uint64_t kept = 0;
    for (auto& count : histogram) {
        count = std::min(count, limit);
        kept += count;
    }
    const auto excess = samples.size() - kept;
    std::uint64_t cumulative = 0;
    for (std::size_t bin = 0; bin < histogram.size(); ++bin) {
        cumulative += histogram.at(bin) + (excess / 1024) + std::uint64_t{bin < excess % 1024};
        map.knots.at(bin + 1) =
            static_cast<double>(cumulative) / static_cast<double>(samples.size());
    }
    require(cumulative == samples.size(), "independent CLAHE histogram retains all integer mass");
    return map;
}
inline std::vector<double> clahe_reference_samples(ClaheFixture& fixture,
                                                   std::array<std::uint32_t, 4> bounds) {
    std::vector<double> samples;
    for (auto y = bounds.at(2); y < bounds.at(3); ++y) {
        for (auto x = bounds.at(0); x < bounds.at(1); ++x) {
            if (fixture.protection.view().row(y).subspan(x, 1).front() == 0) {
                samples.push_back(sharpen_reference_srgb_encode(
                    fixture.luminance.view().row(y).subspan(x, 1).front()));
            }
        }
    }
    return samples;
}
inline std::vector<ClaheReferenceMap> clahe_reference_maps(ClaheFixture& fixture,
                                                           methods::ClaheParameters p) {
    std::vector<ClaheReferenceMap> maps;
    maps.reserve(std::size_t{p.grid_columns} * p.grid_rows);
    const auto width = fixture.luminance.width();
    const auto height = fixture.luminance.height();
    for (std::uint32_t j = 0; j < p.grid_rows; ++j) {
        for (std::uint32_t i = 0; i < p.grid_columns; ++i) {
            const auto samples =
                clahe_reference_samples(fixture, {
                                                     i * width / p.grid_columns,
                                                     (i + 1) * width / p.grid_columns,
                                                     j * height / p.grid_rows,
                                                     (j + 1) * height / p.grid_rows,
                                                 });
            maps.push_back(clahe_reference_fit(samples, p.clip));
        }
    }
    return maps;
}
struct ClaheReferenceAxis {
    std::uint32_t first;
    std::uint32_t second;
    double weight;
};
inline ClaheReferenceAxis clahe_reference_axis(std::uint32_t coordinate, std::uint32_t size,
                                               std::uint32_t divisions) {
    std::vector<double> centers;
    centers.reserve(divisions);
    for (std::uint32_t i = 0; i < divisions; ++i) {
        const auto left = i * size / divisions;
        const auto right = (i + 1) * size / divisions;
        centers.push_back((static_cast<double>(left) + static_cast<double>(right) - 1) / 2);
    }
    const auto upper = std::ranges::upper_bound(centers, static_cast<double>(coordinate));
    if (upper == centers.begin()) {
        return {.first = 0, .second = 0, .weight = 0};
    }
    if (upper == centers.end()) {
        return {.first = divisions - 1, .second = divisions - 1, .weight = 0};
    }
    const auto right = static_cast<std::uint32_t>(upper - centers.begin());
    return {
        .first = right - 1,
        .second = right,
        .weight =
            (coordinate - centers.at(right - 1)) / (centers.at(right) - centers.at(right - 1)),
    };
}
inline double clahe_reference_context(double f, const ClaheReferenceMap& map) {
    if (map.identity) {
        return f;
    }
    const auto scaled = f * 1024;
    const auto bin = std::min(std::size_t{1023}, static_cast<std::size_t>(std::floor(scaled)));
    return map.knots.at(bin) +
           ((scaled - static_cast<double>(bin)) * (map.knots.at(bin + 1) - map.knots.at(bin)));
}
inline double clahe_reference_interpolate(double first, double second, double weight) {
    return first == second ? first : first + (weight * (second - first));
}
inline double clahe_reference_candidate(double f, std::array<ClaheReferenceAxis, 2> axes,
                                        std::span<const ClaheReferenceMap> maps,
                                        std::uint32_t columns) {
    const auto horizontal = axes.front();
    const auto vertical = axes.back();
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        return clahe_reference_context(f, maps.subspan((y * columns) + x, 1).front());
    };
    const auto upper =
        clahe_reference_interpolate(at(horizontal.first, vertical.first),
                                    at(horizontal.second, vertical.first), horizontal.weight);
    const auto lower =
        clahe_reference_interpolate(at(horizontal.first, vertical.second),
                                    at(horizontal.second, vertical.second), horizontal.weight);
    return clahe_reference_interpolate(upper, lower, vertical.weight);
}
struct ClaheReference {
    std::vector<double> samples;
    methods::ClaheFacts facts;
};
inline void clahe_reference_map_row(ClaheFixture& fixture, std::uint32_t y,
                                    methods::ClaheParameters p,
                                    std::span<const ClaheReferenceMap> maps,
                                    ClaheReference& result) {
    const auto view = fixture.luminance.view();
    const auto vertical = clahe_reference_axis(y, view.height(), p.grid_rows);
    for (std::uint32_t x = 0; x < view.width(); ++x) {
        if (fixture.protection.view().row(y).subspan(x, 1).front() != 0) {
            continue;
        }
        const auto f = sharpen_reference_srgb_encode(view.row(y).subspan(x, 1).front());
        const auto horizontal = clahe_reference_axis(x, view.width(), p.grid_columns);
        const auto candidate =
            clahe_reference_candidate(f, {horizontal, vertical}, maps, p.grid_columns);
        require(std::isfinite(candidate) && candidate >= 0 && candidate <= 1,
                "independent CLAHE output is finite unit");
        ++result.facts.counts.evaluated;
        if (candidate != f) {
            const auto next = ((1 - p.blend) * f) + (p.blend * candidate);
            if (next != f) {
                result.samples.at((std::size_t{y} * view.row_pitch()) + x) =
                    sharpen_reference_srgb_decode(next);
                ++result.facts.counts.changed;
            }
        }
    }
}
inline ClaheReference clahe_reference(ClaheFixture& fixture, methods::ClaheParameters p) {
    ClaheReference result;
    const auto view = fixture.luminance.view();
    result.samples.assign(view.storage().begin(), view.storage().end());
    const auto protection = fixture.protection.view();
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        const auto row = protection.row(y);
        for (const auto mask : row) {
            if (mask == 0) {
                ++result.facts.counts.eligible;
            } else {
                ++result.facts.counts.protected_samples;
            }
        }
    }
    if (p.blend == 0) {
        result.facts.reason = methods::ClaheReason::zero_blend;
        return result;
    }
    if (result.facts.counts.eligible == 0) {
        return result;
    }
    const auto maps = clahe_reference_maps(fixture, p);
    for (const auto& map : maps) {
        result.facts.identity_tiles += static_cast<std::uint32_t>(map.identity);
    }
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        clahe_reference_map_row(fixture, y, p, maps, result);
    }
    result.facts.reason = result.facts.counts.changed == 0 ? methods::ClaheReason::no_effect
                                                           : methods::ClaheReason::none;
    result.facts.disposition = result.facts.counts.changed == 0 ? methods::Disposition::no_change
                                                                : methods::Disposition::applied;
    return result;
}
} // namespace docenhance::tests
