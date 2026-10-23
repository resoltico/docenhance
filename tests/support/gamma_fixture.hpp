// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/gamma.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
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
inline std::size_t gamma_fixture_bytes(std::uint32_t width, std::uint32_t height) {
    const auto y = image::plane_shape(width, height, sizeof(double)).value();
    const auto mask = image::plane_shape(width, height, sizeof(std::uint8_t)).value();
    return image::plane_bytes(y).value() + image::plane_bytes(mask).value();
}
struct GammaFixture {
    GammaFixture(std::uint32_t width, std::uint32_t height)
        : budget(gamma_fixture_bytes(width, height)),
          luminance(image::Plane<double>::allocate(budget, width, height).value()),
          protection(image::Plane<std::uint8_t>::allocate(budget, width, height).value()) {
        std::ranges::fill(luminance.view().storage(), -1.0);
        std::ranges::fill(protection.view().storage(), std::uint8_t{255});
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const bool protected_sample = (x + (3 * y)) % 7 == 0 || (y >= 10 && y < 16);
                luminance.view().row(y).subspan(x, 1).front() =
                    protected_sample
                        ? -0.0
                        : static_cast<double>(1 + (((17 * x) + (13 * y)) % 997)) / 1000;
                protection.view().row(y).subspan(x, 1).front() = protected_sample ? 255 : 0;
            }
        }
    }
    core::Budget budget;
    image::Plane<double> luminance;
    image::Plane<std::uint8_t> protection;
};
inline std::vector<std::uint64_t> gamma_bits(std::span<const double> values) {
    std::vector<std::uint64_t> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(std::bit_cast<std::uint64_t>(value));
    }
    return result;
}
struct GammaReference {
    std::vector<double> samples;
    methods::GammaFacts facts;
};
inline methods::PixelCounts gamma_reference_cohort(GammaFixture& fixture) {
    methods::PixelCounts counts;
    const auto protection = fixture.protection.view();
    for (std::uint32_t y = 0; y < protection.height(); ++y) {
        const auto row = protection.row(y);
        for (const auto sample : row) {
            if (sample == 0) {
                ++counts.eligible;
            } else {
                ++counts.protected_samples;
            }
        }
    }
    return counts;
}
struct GammaReferenceStep {
    double value;
    bool written;
};
inline GammaReferenceStep gamma_reference_step(double entering,
                                               methods::GammaParameters parameters) {
    require(std::isfinite(entering) && entering >= 0 && entering <= 1,
            "the standalone fixture establishes Working's input domain");
    const auto f = sharpen_reference_srgb_encode(entering);
    const auto candidate = f == 0 || f == 1 ? f : std::pow(f, parameters.gamma);
    if (candidate == f) {
        return {.value = entering, .written = false};
    }
    const auto next = ((1 - parameters.blend) * f) + (parameters.blend * candidate);
    if (next == f) {
        return {.value = entering, .written = false};
    }
    return {.value = sharpen_reference_srgb_decode(next), .written = true};
}
inline GammaReference gamma_reference(GammaFixture& fixture, methods::GammaParameters parameters) {
    const auto view = fixture.luminance.view();
    GammaReference result;
    result.samples.assign(view.storage().begin(), view.storage().end());
    result.facts.counts = gamma_reference_cohort(fixture);
    if (parameters.blend == 0) {
        result.facts.reason = methods::GammaReason::zero_blend;
        return result;
    }
    if (result.facts.counts.eligible == 0) {
        return result;
    }
    if (parameters.gamma == 1) {
        result.facts.reason = methods::GammaReason::identity_gamma;
        return result;
    }
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        for (std::uint32_t x = 0; x < view.width(); ++x) {
            if (fixture.protection.view().row(y).subspan(x, 1).front() != 0) {
                continue;
            }
            const auto step = gamma_reference_step(view.row(y).subspan(x, 1).front(), parameters);
            ++result.facts.counts.evaluated;
            if (step.written) {
                result.samples.at((std::size_t{y} * view.row_pitch()) + x) = step.value;
                ++result.facts.counts.changed;
            }
        }
    }
    result.facts.reason = result.facts.counts.changed == 0 ? methods::GammaReason::no_effect
                                                           : methods::GammaReason::none;
    result.facts.disposition = result.facts.counts.changed == 0 ? methods::Disposition::no_change
                                                                : methods::Disposition::applied;
    return result;
}
inline methods::GammaFacts check_gamma_reference(GammaFixture& fixture,
                                                 methods::GammaParameters parameters,
                                                 unsigned workers) {
    const auto expected = gamma_reference(fixture, parameters);
    const auto original = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> original_mask{mask.begin(), mask.end()};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
    const auto method = methods::Gamma::create(parameters).value();
    const auto charge = fixture.budget.used();
    require(fixture.budget.available() == 0, "Gamma starts with no remaining payload charge");
    const auto facts = methods::apply(method, {
                                                  fixture.luminance.view(),
                                                  fixture.protection.view().as_const(),
                                                  fixture.budget,
                                                  scheduler,
                                              });
    require(facts.has_value(), "the actual Working Gamma succeeds");
    require(*facts == expected.facts && methods::validate(*facts, parameters),
            "Gamma returns actual contextual counts and closed reasons");
    const auto view = fixture.luminance.view();
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        for (std::size_t x = 0; x < view.row_pitch(); ++x) {
            const auto index = (std::size_t{y} * view.row_pitch()) + x;
            const auto actual = view.storage().subspan(index, 1).front();
            const bool untouched =
                x >= view.width() || fixture.protection.view().row(y).subspan(x, 1).front() != 0 ||
                expected.facts.counts.evaluated == 0 ||
                std::bit_cast<std::uint64_t>(expected.samples.at(index)) == original.at(index);
            if (untouched) {
                require(std::bit_cast<std::uint64_t>(actual) == original.at(index),
                        "Gamma preserves protected, bypassed and padding bits");
            } else {
                require(std::isfinite(actual) && actual >= 0 && actual <= 1,
                        "actual Gamma output is finite unit luminance");
                require(std::abs(actual - expected.samples.at(index)) < 1e-12,
                        "Working Gamma matches independent transfer power blend decode");
            }
        }
    }
    require(std::ranges::equal(mask, original_mask), "Gamma never mutates the protection backing");
    require(fixture.budget.used() == charge && fixture.budget.available() == 0,
            "Gamma needs zero charged method scratch");
    return *facts;
}
} // namespace docenhance::tests
