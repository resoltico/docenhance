// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "nlm_working_fixture.hpp"
#include "sharpen_reference.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
namespace docenhance::tests {
struct NlmReference {
    std::vector<double> samples;
    std::vector<std::uint16_t> codes;
    methods::NlmFacts facts;
};
inline std::uint32_t nlm_reference_fold(std::int64_t index, std::uint32_t extent) {
    if (extent == 1) {
        return 0;
    }
    while (index < 0 || std::cmp_greater_equal(index, extent)) {
        index = index < 0 ? -index : (2 * static_cast<std::int64_t>(extent)) - 2 - index;
    }
    return static_cast<std::uint32_t>(index);
}
inline std::uint16_t nlm_reference_sample(const NlmWorkingFixture& fixture,
                                          const NlmReference& reference, std::int64_t x,
                                          std::int64_t y) {
    return reference.codes.at((std::size_t{nlm_reference_fold(y, fixture.luminance.height())} *
                               fixture.luminance.width()) +
                              nlm_reference_fold(x, fixture.luminance.width()));
}
struct NlmReferenceOffset {
    std::int32_t x;
    std::int32_t y;
};
inline std::uint32_t nlm_reference_distance(const NlmWorkingFixture& fixture,
                                            const NlmReference& reference, image::Coordinate point,
                                            NlmReferenceOffset offset, std::uint32_t patch) {
    const auto x = static_cast<std::int64_t>(point.x);
    const auto y = static_cast<std::int64_t>(point.y);
    std::uint32_t distance = 0;
    const auto radius = static_cast<std::int32_t>(patch / 2);
    for (auto ky = -radius; ky <= radius; ++ky) {
        for (auto kx = -radius; kx <= radius; ++kx) {
            const auto first =
                static_cast<std::int32_t>(nlm_reference_sample(fixture, reference, x + kx, y + ky));
            const auto second = static_cast<std::int32_t>(
                nlm_reference_sample(fixture, reference, x + offset.x + kx, y + offset.y + ky));
            distance += static_cast<std::uint32_t>(std::abs(first - second));
        }
    }
    return distance;
}
inline double nlm_reference_mean(const NlmWorkingFixture& fixture, const NlmReference& reference,
                                 std::uint32_t x, std::uint32_t y, methods::NlmParameters p) {
    const auto radius = static_cast<std::int32_t>(p.search / 2);
    const double eta = p.h / 255;
    const double samples = p.patch * p.patch;
    const double cutoff = std::ceil(3 * eta * samples * 65535);
    double numerator = 0;
    double denominator = 0;
    for (auto oy = -radius; oy <= radius; ++oy) {
        for (auto ox = -radius; ox <= radius; ++ox) {
            const auto distance = nlm_reference_distance(fixture, reference, {.x = x, .y = y},
                                                         {.x = ox, .y = oy}, p.patch);
            if (distance >= cutoff) {
                continue;
            }
            const double t = distance / (samples * 65535.0 * eta);
            const auto weight = std::exp(-(t * t));
            numerator += weight * nlm_reference_sample(fixture, reference, std::int64_t{x} + ox,
                                                       std::int64_t{y} + oy);
            denominator += weight;
        }
    }
    return numerator / denominator;
}
inline void nlm_reference_map(NlmWorkingFixture& fixture, methods::NlmParameters p,
                              NlmReference& reference) {
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        const auto row = fixture.luminance.view().row(y);
        const auto mask = fixture.protection.view().row(y);
        for (std::uint32_t x = 0; x < row.size(); ++x) {
            if (mask.subspan(x, 1).front() != 0) {
                continue;
            }
            ++reference.facts.counts.evaluated;
            const auto mean = nlm_reference_mean(fixture, reference, x, y, p);
            if (mean == reference.codes.at((std::size_t{y} * fixture.luminance.width()) + x)) {
                continue;
            }
            ++reference.facts.corrected_samples;
            const auto f = sharpen_reference_srgb_encode(row.subspan(x, 1).front());
            const auto delta =
                (mean - reference.codes.at((std::size_t{y} * fixture.luminance.width()) + x)) /
                65535;
            const auto corrected = std::clamp(f + delta, 0.0, 1.0);
            const auto next = ((1 - p.blend) * f) + (p.blend * corrected);
            if (next != f) {
                reference.samples.at((std::size_t{y} * fixture.luminance.view().row_pitch()) + x) =
                    sharpen_reference_srgb_decode(next);
                ++reference.facts.counts.changed;
            }
        }
    }
}
inline NlmReference nlm_working_reference(NlmWorkingFixture& fixture, methods::NlmParameters p) {
    const auto storage = fixture.luminance.view().storage();
    NlmReference reference{};
    reference.samples.assign(storage.begin(), storage.end());
    reference.codes.reserve(std::size_t{fixture.luminance.width()} * fixture.luminance.height());
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        const auto row = fixture.luminance.view().row(y);
        const auto mask = fixture.protection.view().row(y);
        for (std::uint32_t x = 0; x < row.size(); ++x) {
            reference.facts.counts.eligible +=
                static_cast<std::uint64_t>(mask.subspan(x, 1).front() == 0);
            reference.facts.counts.protected_samples +=
                static_cast<std::uint64_t>(mask.subspan(x, 1).front() != 0);
            reference.codes.push_back(static_cast<std::uint16_t>(std::floor(
                (65535 * sharpen_reference_srgb_encode(row.subspan(x, 1).front())) + 0.5)));
        }
    }
    if (p.blend == 0) {
        reference.facts.reason = methods::NlmReason::zero_blend;
        return reference;
    }
    if (reference.facts.counts.eligible == 0) {
        return reference;
    }
    const double eta = p.h / 255;
    const double samples = p.patch * p.patch;
    reference.facts.weight_table_entries =
        static_cast<std::uint32_t>(std::ceil(3 * eta * samples * 65535));
    nlm_reference_map(fixture, p, reference);
    reference.facts.disposition = reference.facts.counts.changed == 0
                                      ? methods::Disposition::no_change
                                      : methods::Disposition::applied;
    reference.facts.reason = reference.facts.counts.changed == 0 ? methods::NlmReason::no_effect
                                                                 : methods::NlmReason::none;
    return reference;
}
} // namespace docenhance::tests
