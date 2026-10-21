// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "nlm_resource_cases.hpp"

#include <array>

namespace docenhance::tests {
inline NlmReference nlm_extreme_input(NlmWorkingFixture& fixture) {
    NlmReference reference{};
    const auto plane = fixture.luminance.view();
    const auto storage = plane.storage();
    reference.samples.assign(storage.begin(), storage.end());
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        for (const auto value : plane.row(y)) {
            reference.codes.push_back(static_cast<std::uint16_t>(
                std::floor((65535 * sharpen_reference_srgb_encode(value)) + 0.5)));
        }
    }
    return reference;
}
inline double nlm_extreme_expected(NlmWorkingFixture& fixture, const NlmReference& reference,
                                   image::Coordinate point, methods::NlmParameters p) {
    const auto index = (std::size_t{point.y} * fixture.luminance.view().row_pitch()) + point.x;
    const auto entering = reference.samples.at(index);
    if (fixture.protection.view().row(point.y)[point.x] != 0) {
        return entering;
    }
    const auto mean = nlm_reference_mean(fixture, reference, point.x, point.y, p);
    const auto code =
        reference.codes.at((std::size_t{point.y} * fixture.luminance.width()) + point.x);
    if (mean == code) {
        return entering;
    }
    const auto f = sharpen_reference_srgb_encode(entering);
    const auto corrected = std::clamp(f + ((mean - code) / 65535), 0.0, 1.0);
    const auto next = ((1 - p.blend) * f) + (p.blend * corrected);
    return next == f ? entering : sharpen_reference_srgb_decode(next);
}
inline void nlm_resource_extreme(methods::NlmParameters p) {
    NlmWorkingFixture fixture{41, 129, nlm_working_test_scratch};
    fixture.protection.view().row(0)[0] = 255;
    const NlmBacking before{fixture};
    const auto reference = nlm_extreme_input(fixture);
    constexpr std::array<std::uint32_t, 3> xs{0, 20, 40};
    constexpr std::array<std::uint32_t, 6> ys{0, 63, 64, 126, 127, 128};
    std::array<double, xs.size() * ys.size()> expected{};
    std::size_t index = 0;
    for (const auto y : ys) {
        for (const auto x : xs) {
            expected.at(index++) = nlm_extreme_expected(fixture, reference, {.x = x, .y = y}, p);
        }
    }
    const auto concurrency = exec::Concurrency::resolve(2, 2, 0, 0);
    require(concurrency.has_value(), "extreme selector has an admitted two-worker scheduler");
    const exec::Scheduler scheduler{*concurrency};
    const auto result = run_nlm(fixture, p, scheduler);
    require(result && methods::validate(*result, p) && result->counts.eligible == 5288 &&
                result->counts.evaluated == 5288 && result->counts.protected_samples == 1 &&
                result->counts.changed > 0 && fixture.budget.used() == before.resident,
            "all four extreme selectors cover three real bands with valid whole-frame domains");
    index = 0;
    for (const auto y : ys) {
        for (const auto x : xs) {
            require(std::abs(fixture.luminance.view().row(y)[x] - expected.at(index++)) < 1e-12,
                    "independent direct patch oracle covers corners, seams and last-row tail");
        }
    }
    const auto output = fixture.luminance.view().storage();
    const auto pitch = fixture.luminance.view().row_pitch();
    for (std::size_t i = 0; i < output.size(); ++i) {
        if (i == 0 || i % pitch >= fixture.luminance.width()) {
            require(std::bit_cast<std::uint64_t>(output[i]) == before.y.at(i),
                    "extreme selector preserves protected and padded Y bits exactly");
        }
    }
    require(std::ranges::equal(fixture.protection.view().storage(), before.mask),
            "extreme selector preserves complete protection backing");
}
inline void nlm_resource_extremes() {
    for (const auto patch : {3U, 15U}) {
        for (const auto search : {21U, 41U}) {
            nlm_resource_extreme({.patch = patch, .search = search});
        }
    }
}
} // namespace docenhance::tests
