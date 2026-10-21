// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/unsharp.hpp"
#include "docenhance/methods/warnings.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "unsharp_fixture.hpp"
#include "unsharp_reference.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace docenhance::tests {
inline constexpr methods::UnsharpParameters default_unsharp_parameters{};
inline bool unsharp_reference_facts_match(const methods::UnsharpFacts& actual,
                                          const methods::UnsharpFacts& expected) {
    const bool counts = actual.disposition == expected.disposition &&
                        actual.reason == expected.reason && actual.counts == expected.counts &&
                        actual.context_samples == expected.context_samples &&
                        actual.corrected_samples == expected.corrected_samples &&
                        actual.clipped_low_samples == expected.clipped_low_samples &&
                        actual.clipped_high_samples == expected.clipped_high_samples;
    if (!counts || actual.pre_clamp.has_value() != expected.pre_clamp.has_value()) {
        return false;
    }
    return !actual.pre_clamp ||
           (expected.pre_clamp &&
            std::abs(actual.pre_clamp->low - expected.pre_clamp->low) < 1e-12 &&
            std::abs(actual.pre_clamp->high - expected.pre_clamp->high) < 1e-12);
}
inline methods::UnsharpFacts check_unsharp_reference(UnsharpFixture& fixture,
                                                     methods::UnsharpParameters parameters,
                                                     unsigned workers) {
    const auto expected = unsharp_reference(fixture, parameters);
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> original_mask{mask.begin(), mask.end()};
    const auto charge = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
    const auto result =
        methods::apply(methods::Unsharp::create(parameters).value(),
                       {fixture.luminance.view(), fixture.protection.view().as_const(),
                        fixture.budget, scheduler});
    require(result.has_value(), "actual Working unsharp succeeds");
    require(
        unsharp_reference_facts_match(*result, expected.facts) &&
            methods::validate(*result, parameters),
        "exact tags/counts and independently bounded raw extrema agree with direct2D reference");
    require(methods::warnings(*result).front().has_value() == (parameters.amount > 0),
            "sharpening warning remains derived for every actual positive request including "
            "no-effect and E0");
    const auto actual = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < expected.samples.size(); ++i) {
        const auto value = actual.subspan(i, 1).front();
        if (std::bit_cast<std::uint64_t>(expected.samples.at(i)) == before.at(i)) {
            require(std::bit_cast<std::uint64_t>(value) == before.at(i),
                    "unsharp preserves exact protected padding and identity backing bits");
        } else {
            require(std::isfinite(value) && value >= 0 && value <= 1 &&
                        std::abs(value - expected.samples.at(i)) < 2e-12,
                    "unsharp Y output agrees with independent direct2D blur and raw threshold");
        }
    }
    require(std::ranges::equal(mask, original_mask), "unsharp never writes protection storage");
    require(fixture.budget.used() == charge, "unsharp refunds both fields after joined return");
    return *result;
}
inline void unsharp_protect_fixture(UnsharpFixture& fixture) {
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        for (std::uint32_t x = 0; x < fixture.luminance.width(); ++x) {
            if ((x + (3 * y)) % 7 == 0 || (y >= 10 && y < 16)) {
                fixture.protection.view().row(y).subspan(x, 1).front() = 255;
                fixture.luminance.view().row(y).subspan(x, 1).front() = -0.0;
            }
        }
    }
}
inline void unsharp_workers_case(std::uint32_t width, std::uint32_t height,
                                 methods::UnsharpParameters parameters) {
    std::vector<std::uint64_t> baseline;
    methods::UnsharpFacts original;
    for (const unsigned workers : {1U, 2U, 7U}) {
        UnsharpFixture fixture{width, height, 2 * unsharp_plane_bytes(width, height)};
        unsharp_protect_fixture(fixture);
        const auto facts = check_unsharp_reference(fixture, parameters, workers);
        require(facts.counts.changed != 0, "unsharp reference exercises actual active writes");
        const auto bits = gamma_bits(fixture.luminance.view().storage());
        if (baseline.empty()) {
            baseline = bits;
            original = facts;
        } else {
            require(
                bits == baseline && facts == original,
                "gather both Gaussian passes mapping and facts are bit-identical for1/2/7 workers");
        }
    }
}
inline void unsharp_active_cases() {
    unsharp_workers_case(33, 137, default_unsharp_parameters);
    unsharp_workers_case(17, 19, {.sigma = 3, .amount = 1.5, .threshold = 0.5});
    unsharp_workers_case(1, 129, {.sigma = 3, .amount = 0.7, .threshold = 1});
    unsharp_workers_case(129, 1, {.sigma = 0.3, .amount = 2, .threshold = 0.5});
    unsharp_workers_case(2, 3, {.sigma = 3, .amount = 1.5, .threshold = 0.5});
}
inline void unsharp_constant_and_rounding_cases() {
    UnsharpFixture constant{5, 3, 2 * unsharp_plane_bytes(5, 3)};
    for (std::uint32_t y = 0; y < 3; ++y) {
        std::ranges::fill(constant.luminance.view().row(y), 0.4);
    }
    const auto identity = check_unsharp_reference(constant, {.threshold = 0}, 7);
    require(identity.counts.evaluated == 15 && identity.context_samples == 15 &&
                identity.corrected_samples == 0 && identity.counts.changed == 0 &&
                identity.pre_clamp,
            "active constants fit and evaluate actual full context without corrections");
    UnsharpFixture singleton{1, 1, 2 * unsharp_plane_bytes(1, 1)};
    check_unsharp_reference(singleton, {.sigma = 3, .threshold = 0}, 7);
    UnsharpFixture tiny{7, 3, 2 * unsharp_plane_bytes(7, 3)};
    const auto rounded = check_unsharp_reference(
        tiny, {.amount = std::numeric_limits<double>::denorm_min(), .threshold = 0}, 1);
    require(rounded.context_samples == 21 && rounded.counts.evaluated == 21 &&
                rounded.counts.changed == 0 && rounded.corrected_samples == 0,
            "tiny positive amount remains valid active fitted no-effect work");
}
inline void unsharp_endpoint_case() {
    UnsharpFixture fixture{3, 1, 2 * unsharp_plane_bytes(3, 1)};
    fixture.luminance.view().row(0).front() = 0;
    fixture.luminance.view().row(0).subspan(1, 1).front() = 1;
    fixture.luminance.view().row(0).back() = 0;
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto facts =
        check_unsharp_reference(fixture, {.sigma = 0.8, .amount = 1, .threshold = 0}, 7);
    require(
        facts.pre_clamp && facts.pre_clamp->low < 0 && facts.pre_clamp->high > 1 &&
            facts.corrected_samples == 3 && facts.clipped_low_samples == 2 &&
            facts.clipped_high_samples == 1 && facts.counts.changed == 1,
        "actual black-white-black raw signs and D3 counts include the white one-over-one store");
    require(gamma_bits(fixture.luminance.view().storage()) == before,
            "clipped endpoint Y bits may all stay equal while a true perceptual write occurred");
}
inline void unsharp_context_case() {
    UnsharpFixture low{7, 3, 2 * unsharp_plane_bytes(7, 3)};
    UnsharpFixture high{7, 3, 2 * unsharp_plane_bytes(7, 3)};
    for (std::uint32_t y = 0; y < 3; ++y) {
        std::ranges::fill(low.luminance.view().row(y), 0.4);
        std::ranges::fill(high.luminance.view().row(y), 0.4);
    }
    low.protection.view().row(1).subspan(3, 1).front() = 255;
    high.protection.view().row(1).subspan(3, 1).front() = 255;
    low.luminance.view().row(1).subspan(3, 1).front() = -0.0;
    high.luminance.view().row(1).subspan(3, 1).front() = 1;
    const auto a = check_unsharp_reference(low, {.threshold = 0}, 1);
    const auto b = check_unsharp_reference(high, {.threshold = 0}, 7);
    require(
        a.context_samples == 21 && b.context_samples == 21 && a.counts.eligible == 20 &&
            low.luminance.view().row(1).subspan(2, 1).front() !=
                high.luminance.view().row(1).subspan(2, 1).front(),
        "a protected impulse participates in Gaussian context and changes its eligible neighbor");
}
} // namespace docenhance::tests
