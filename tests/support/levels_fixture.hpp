// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/levels.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "sharpen_reference.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace docenhance::tests {
inline std::size_t levels_sample_bytes(std::size_t eligible) {
    return image::plane_bytes(
               image::plane_shape(static_cast<std::uint32_t>(eligible), 1, sizeof(double)).value())
        .value();
}
struct LevelsFixture {
    LevelsFixture(std::uint32_t width, std::uint32_t height, std::size_t scratch)
        : budget(gamma_fixture_bytes(width, height) + scratch),
          luminance(image::Plane<double>::allocate(budget, width, height).value()),
          protection(image::Plane<std::uint8_t>::allocate(budget, width, height).value()) {
        std::ranges::fill(luminance.view().storage(), -1.0);
        std::ranges::fill(protection.view().storage(), 255);
        for (std::uint32_t y = 0; y < height; ++y) {
            std::ranges::fill(protection.view().row(y), 0);
            for (std::uint32_t x = 0; x < width; ++x) {
                luminance.view().row(y).subspan(x, 1).front() =
                    static_cast<double>((17 * x) + (13 * y)) /
                    static_cast<double>((17 * width) + (13 * height));
            }
        }
    }
    core::Budget budget;
    image::Plane<double> luminance;
    image::Plane<std::uint8_t> protection;
};
struct LevelsReference {
    std::vector<double> samples;
    methods::LevelsFacts facts;
};
inline methods::LevelsRange levels_reference_fit(LevelsFixture& fixture,
                                                 methods::LevelsParameters parameters) {
    std::vector<double> eligible;
    const auto view = fixture.luminance.view();
    const auto protection = fixture.protection.view();
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        for (std::uint32_t x = 0; x < view.width(); ++x) {
            if (protection.row(y).subspan(x, 1).front() != 0) {
                continue;
            }
            eligible.push_back(sharpen_reference_srgb_encode(view.row(y).subspan(x, 1).front()));
        }
    }
    std::ranges::sort(eligible);
    const auto rank = [&](double percentage) {
        const auto value = static_cast<std::size_t>(
            std::ceil((percentage / 100) * static_cast<double>(eligible.size())));
        return value == 0 ? std::size_t{0} : std::min(value - 1, eligible.size() - 1);
    };
    return methods::LevelsRange{
        .low = eligible.at(rank(parameters.low)),
        .high = eligible.at(rank(parameters.high)),
    };
}
inline void levels_reference_map(LevelsFixture& fixture, methods::LevelsParameters parameters,
                                 LevelsReference& result, methods::LevelsRange range) {
    const auto view = fixture.luminance.view();
    const auto protection = fixture.protection.view();
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        for (std::uint32_t x = 0; x < view.width(); ++x) {
            if (protection.row(y).subspan(x, 1).front() != 0) {
                continue;
            }
            const auto original = view.row(y).subspan(x, 1).front();
            const auto f = sharpen_reference_srgb_encode(original);
            const auto candidate = std::clamp((f - range.low) / (range.high - range.low), 0.0, 1.0);
            const auto next = ((1 - parameters.blend) * f) + (parameters.blend * candidate);
            ++result.facts.counts.evaluated;
            result.facts.clipped_low_samples += static_cast<std::uint64_t>(f < range.low);
            result.facts.clipped_high_samples += static_cast<std::uint64_t>(f > range.high);
            if (candidate != f && next != f) {
                result.samples.at((std::size_t{y} * view.row_pitch()) + x) =
                    sharpen_reference_srgb_decode(next);
                ++result.facts.counts.changed;
            }
        }
    }
}
inline LevelsReference levels_reference(LevelsFixture& fixture,
                                        methods::LevelsParameters parameters) {
    LevelsReference result;
    const auto view = fixture.luminance.view();
    const auto protection = fixture.protection.view();
    result.samples.assign(view.storage().begin(), view.storage().end());
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        const auto row = protection.row(y);
        for (const auto value : row) {
            if (value == 0) {
                ++result.facts.counts.eligible;
            } else {
                ++result.facts.counts.protected_samples;
            }
        }
    }
    if (parameters.blend == 0) {
        result.facts.reason = methods::LevelsReason::zero_blend;
        return result;
    }
    if (result.facts.counts.eligible == 0) {
        return result;
    }
    const auto range = levels_reference_fit(fixture, parameters);
    result.facts.levels = range;
    if (range.high - range.low < methods::levels_minimum_range) {
        result.facts.reason = methods::LevelsReason::insufficient_dynamic_range;
        return result;
    }
    levels_reference_map(fixture, parameters, result, range);
    result.facts.reason = result.facts.counts.changed == 0 ? methods::LevelsReason::no_effect
                                                           : methods::LevelsReason::none;
    result.facts.disposition = result.facts.counts.changed == 0 ? methods::Disposition::no_change
                                                                : methods::Disposition::applied;
    return result;
}
inline methods::LevelsFacts check_levels_reference(LevelsFixture& fixture,
                                                   methods::LevelsParameters parameters,
                                                   unsigned workers) {
    const auto expected = levels_reference(fixture, parameters);
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> original_mask{mask.begin(), mask.end()};
    const auto charge = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
    const auto method = methods::Levels::create(parameters).value();
    const auto result =
        methods::apply(method, {fixture.luminance.view(), fixture.protection.view().as_const(),
                                fixture.budget, scheduler});
    require(result.has_value(), "actual Working Levels succeeds");
    require(*result == expected.facts && methods::validate(*result, parameters),
            "Levels returns the actual fit and contextual counts");
    const auto view = fixture.luminance.view();
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        for (std::size_t x = 0; x < view.row_pitch(); ++x) {
            const auto index = (std::size_t{y} * view.row_pitch()) + x;
            const auto value = view.storage().subspan(index, 1).front();
            if (x >= view.width() || fixture.protection.view().row(y).subspan(x, 1).front() != 0 ||
                std::bit_cast<std::uint64_t>(expected.samples.at(index)) == before.at(index)) {
                require(std::bit_cast<std::uint64_t>(value) == before.at(index),
                        "Levels preserves protected, padding and no-write bits");
            } else {
                require(std::isfinite(value) && value >= 0 && value <= 1,
                        "Levels output is finite unit data");
                require(std::abs(value - expected.samples.at(index)) < 1e-12,
                        "Levels agrees with independent sorted quantile and mapping reference");
            }
        }
    }
    require(std::ranges::equal(mask, original_mask), "Levels never mutates mask backing");
    require(fixture.budget.used() == charge,
            "Levels releases all method scratch after useful lifetime");
    return *result;
}
} // namespace docenhance::tests
