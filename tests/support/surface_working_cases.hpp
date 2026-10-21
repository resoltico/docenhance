// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/surface_working.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "surface_working_fixture.hpp"
#include "surface_working_reference.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace docenhance::tests {
inline auto run_surface(SurfaceWorkingFixture& fixture, methods::SurfaceParameters p,
                        const exec::Scheduler& scheduler, bool mask = true) {
    return methods::apply(
        methods::Surface::create(p).value(),
        {fixture.luminance.view(),
         mask ? fixture.protection.view().as_const() : image::PlaneView<const std::uint8_t>{},
         fixture.budget, scheduler});
}
inline bool surface_measurements_close(const methods::SurfaceMeasurements& a,
                                       const methods::SurfaceMeasurements& b) {
    return a.count == b.count && a.stride == b.stride && a.fallback == b.fallback &&
           std::abs(a.target - b.target) < 1e-6 &&
           std::abs(a.background_q10 - b.background_q10) < 1e-6 &&
           std::abs(a.background_q50 - b.background_q50) < 1e-6 &&
           std::abs(a.background_q90 - b.background_q90) < 1e-6 &&
           a.luminance_q90 == b.luminance_q90 && std::abs(a.variation - b.variation) < 1e-6 &&
           a.paper_fraction == b.paper_fraction && a.dark_fraction == b.dark_fraction;
}
struct SurfaceReferenceApplication {
    std::vector<double> samples;
    std::uint64_t evaluated{};
    std::uint64_t changed{};
    std::uint64_t capped{};
    std::uint64_t saturated{};
    double minimum = 1;
    double maximum = 1;
};
inline SurfaceReferenceApplication
surface_reference_application(SurfaceWorkingFixture& fixture,
                              const SurfaceWorkingReference& reference,
                              methods::SurfaceParameters p, double target) {
    SurfaceReferenceApplication result;
    const auto storage = fixture.luminance.view().storage();
    result.samples.assign(storage.begin(), storage.end());
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        const auto row = fixture.luminance.view().row(y);
        const auto mask = fixture.protection.view().row(y);
        for (std::uint32_t x = 0; x < row.size(); ++x) {
            if (mask.subspan(x, 1).front() != 0) {
                continue;
            }
            const auto b = surface_reference_background(reference, x, y);
            const auto ratio = target / std::max(b, 0.02);
            const auto gain = std::clamp(ratio, 1.0, p.max_gain);
            const auto raw = row.subspan(x, 1).front() * std::pow(gain, p.strength);
            if (result.evaluated == 0) {
                result.minimum = gain;
                result.maximum = gain;
            } else {
                result.minimum = std::min(result.minimum, gain);
                result.maximum = std::max(result.maximum, gain);
            }
            ++result.evaluated;
            result.capped += static_cast<std::uint64_t>(ratio >= p.max_gain);
            result.saturated += static_cast<std::uint64_t>(raw > 1);
            const auto next = std::clamp(raw, 0.0, 1.0);
            if (next != row.subspan(x, 1).front()) {
                result.samples.at((std::size_t{y} * fixture.luminance.view().row_pitch()) + x) =
                    next;
                ++result.changed;
            }
        }
    }
    return result;
}
inline methods::SurfaceFacts check_surface_reference(SurfaceWorkingFixture& fixture,
                                                     methods::SurfaceParameters p,
                                                     unsigned workers) {
    const auto reference = surface_working_reference(fixture, p);
    const auto measured = surface_reference_measurements(fixture, reference, p);
    const auto expected = surface_reference_application(fixture, reference, p, measured.target);
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> mask_before(mask.begin(), mask.end());
    const auto held = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
    const auto actual = run_surface(fixture, p, scheduler);
    require(actual && actual->measurements && actual->solver &&
                methods::validate(
                    *actual, p,
                    {.width = fixture.luminance.width(), .height = fixture.luminance.height()}),
            "actual Working I01 returns complete parameter/extent-admitted numerical Facts");
    if (!actual || !actual->measurements || !actual->solver) {
        return {};
    }
    require(actual->measured_cells == reference.field.measured_cells &&
                actual->dark_cells == reference.field.dark_cells &&
                surface_measurements_close(*actual->measurements, measured) &&
                actual->solver->residual <= actual->solver->tolerance &&
                actual->counts.evaluated == expected.evaluated &&
                actual->counts.changed == expected.changed &&
                actual->gain_capped_samples == expected.capped &&
                actual->saturated_samples == expected.saturated &&
                std::abs(actual->min_gain - expected.minimum) < 1e-6 &&
                std::abs(actual->max_gain - expected.maximum) < 1e-6,
            "independent cell/log-dark/dense/interpolation/gain model retains existing1e-6 output "
            "bounds and attained counters");
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < expected.samples.size(); ++i) {
        const auto value = output.subspan(i, 1).front();
        if (std::bit_cast<std::uint64_t>(expected.samples.at(i)) == before.at(i)) {
            require(std::bit_cast<std::uint64_t>(value) == before.at(i),
                    "protected/identity/padding keep exact original bits");
        } else {
            require(
                std::isfinite(value) && value >= 0 && value <= 1 &&
                    std::abs(value - expected.samples.at(i)) < 1e-6,
                "actual linear Y agrees with independently assembled dense field/gain reference");
        }
    }
    require(std::ranges::equal(mask_before, fixture.protection.view().storage()),
            "actual Working I01 preserves every protection backing byte including padding");
    require(fixture.budget.used() == held,
            "all new Working phase owners refund after joined completion");
    return *actual;
}
inline void surface_protect_fixture(SurfaceWorkingFixture& fixture) {
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        for (std::uint32_t x = 0; x < fixture.luminance.width(); ++x) {
            if ((x + (3 * y)) % 7 == 0 || (y >= 10 && y < 16)) {
                fixture.protection.view().row(y).subspan(x, 1).front() = 255;
                fixture.luminance.view().row(y).subspan(x, 1).front() = -0.0;
            }
        }
    }
}
inline void surface_worker_cases() {
    std::optional<methods::SurfaceFacts> original;
    std::vector<std::uint64_t> bits;
    for (const auto workers : {1U, 2U, 7U}) {
        SurfaceWorkingFixture fixture{33, 129, surface_test_scratch};
        surface_protect_fixture(fixture);
        const auto facts =
            check_surface_reference(fixture, {.max_gain = 4, .target = 0.8, .cell = 8}, workers);
        require(facts.solver && facts.solver->iterations > 0 && facts.counts.changed > 0,
                "worker/cell/map seam fixture actually fits nonconstant data and writes Y");
        const auto current = gamma_bits(fixture.luminance.view().storage());
        if (!original) {
            original = facts;
            bits = current;
        } else {
            require(*original == facts && bits == current,
                    "1/2/7 workers preserve complete actual Facts and whole backing Y bits");
        }
    }
}
inline void surface_absent_mask_case() {
    SurfaceWorkingFixture present{17, 19, surface_test_scratch};
    SurfaceWorkingFixture absent{17, 19, surface_test_scratch};
    const methods::SurfaceParameters p{.target = 0.8, .cell = 8};
    const auto expected = check_surface_reference(present, p, 1);
    const auto held = absent.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto actual = run_surface(absent, p, scheduler, false);
    require(actual && *actual == expected && actual->counts.changed > 0 &&
                gamma_bits(absent.luminance.view().storage()) ==
                    gamma_bits(present.luminance.view().storage()) &&
                absent.budget.used() == held,
            "fresh active empty mask path matches present all-zero mask fullY/Facts/refund");
}
inline void surface_dark_reference_case() {
    SurfaceWorkingFixture fixture{40, 8, surface_test_scratch};
    constexpr std::array values{0.09, 0.23, 0.41, 0.63, 0.89};
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 40; ++x) {
            fixture.luminance.view().row(y).subspan(x, 1).front() = values.at(x / 8);
        }
    }
    const auto facts =
        check_surface_reference(fixture, {.max_gain = 4, .target = 0.93, .cell = 8}, 7);
    require(
        facts.dark_cells == 1 && facts.measured_cells == 4 && facts.solver &&
            facts.solver->iterations > 0 && facts.counts.changed > 0,
        "actual odd five-cell chain excludes dark content and spans it from measured neighbors");
}
inline void surface_full_fallback_case() {
    SurfaceWorkingFixture fixture{1025, 1, surface_test_scratch};
    std::ranges::fill(fixture.protection.view().storage(), 255);
    std::ranges::fill(fixture.luminance.view().row(0), 0.2);
    for (std::uint32_t x = 1; x < 1025; x += 2) {
        fixture.protection.view().row(0).subspan(x, 1).front() = 0;
    }
    const auto facts = check_surface_reference(fixture, {.cell = 32}, 7);
    require(facts.measurements && facts.measurements->fallback && facts.measurements->stride == 2 &&
                facts.measurements->count == 512,
            "I01 fallback retains ALL512 eligible samples rather than I02 first-only fallback");
}
} // namespace docenhance::tests
