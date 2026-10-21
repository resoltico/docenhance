// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/surface_working.hpp"
#include "support/oracle.hpp"
#include "surface_working_fixture.hpp"
#include "surface_working_reference.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace docenhance::fuzz {
inline void illumination_working_pixels(tests::SurfaceWorkingFixture& fixture,
                                        const tests::SurfaceWorkingReference& reference,
                                        const methods::SurfaceMeasurements& measured,
                                        const std::vector<double>& before,
                                        const methods::SurfaceFacts& facts) {
    const methods::SurfaceParameters p{.cell = 8};
    std::uint64_t eligible = 0;
    std::uint64_t changed = 0;
    const auto pitch = fixture.luminance.view().row_pitch();
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < before.size(); ++i) {
        const auto y = static_cast<std::uint32_t>(i / pitch);
        const auto x = static_cast<std::uint32_t>(i % pitch);
        const auto actual = output.subspan(i, 1).front();
        if (x >= fixture.luminance.width() ||
            fixture.protection.view().row(y).subspan(x, 1).front() != 0) {
            require(std::bit_cast<std::uint64_t>(actual) ==
                        std::bit_cast<std::uint64_t>(before.at(i)),
                    "Working I01 protected and padding bits remain exact");
            continue;
        }
        const auto b = tests::surface_reference_background(reference, x, y);
        const auto gain = std::clamp(measured.target / std::max(b, 0.02), 1.0, p.max_gain);
        const auto expected = std::clamp(before.at(i) * std::pow(gain, p.strength), 0.0, 1.0);
        require(std::isfinite(actual) && actual >= 0 && actual <= 1 &&
                    std::abs(actual - expected) < 1e-6,
                "actual Working Y matches independent dense/interpolation/gain output bound");
        ++eligible;
        changed += static_cast<std::uint64_t>(actual != before.at(i));
    }
    require(facts.counts.evaluated == eligible && facts.counts.changed == changed,
            "Working actual evaluation/write events agree with full destination traversal");
}

inline void illumination_working(image::PlaneView<const double> source,
                                 image::PlaneView<const std::uint8_t> mask) {
    tests::SurfaceWorkingFixture fixture{source.width(), source.height(),
                                         tests::surface_test_scratch};
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            // This is a direct admitted linear-Y input, not a production Source/model adapter.
            fixture.luminance.view().row(y).subspan(x, 1).front() =
                source.row(y).subspan(x, 1).front();
            fixture.protection.view().row(y).subspan(x, 1).front() =
                mask.row(y).subspan(x, 1).front();
        }
    }
    const methods::SurfaceParameters p{.cell = 8};
    const auto reference = tests::surface_working_reference(fixture, p);
    const auto measured = reference.field.values.empty()
                              ? methods::SurfaceMeasurements{}
                              : tests::surface_reference_measurements(fixture, reference, p);
    const auto input = fixture.luminance.view().storage();
    const std::vector<double> before(input.begin(), input.end());
    const auto mask_storage = fixture.protection.view().storage();
    const std::vector<std::uint8_t> mask_before(mask_storage.begin(), mask_storage.end());
    const auto held = fixture.budget.used();
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(concurrency.has_value(), "Working I01 concurrency admits");
    const exec::Scheduler scheduler{*concurrency};
    const auto method = methods::Surface::create(p);
    require(method.has_value(), "Working I01 method parameters admit");
    const auto result =
        methods::apply(*method, {fixture.luminance.view(), fixture.protection.view().as_const(),
                                 fixture.budget, scheduler});
    require(std::ranges::equal(mask_before, fixture.protection.view().storage()),
            "Working I01 preserves exact full protection bytes including padding");
    require(fixture.budget.used() == held, "Working I01 all owned phase payloads refund");
    const auto cells = ((source.width() + 7) / 8) * ((source.height() + 7) / 8);
    if (reference.field.measured_cells == 0 || reference.field.measured_cells * 4 < cells) {
        require(!result && result.error().code == core::ErrorCode::method_inapplicable,
                "Working I01 real insufficiency agrees with independent clipped measurements");
        require(std::ranges::equal(before, fixture.luminance.view().storage()),
                "premap insufficiency preserves full backing storage");
        return;
    }
    require(
        result &&
            methods::validate(*result, p, {.width = source.width(), .height = source.height()}) &&
            result->solver && result->solver->residual <= result->solver->tolerance &&
            result->measured_cells == reference.field.measured_cells &&
            result->dark_cells == reference.field.dark_cells && result->measurements &&
            std::abs(result->measurements->target - measured.target) < 1e-6 &&
            result->measurements->luminance_q90 == measured.luminance_q90 &&
            std::abs(result->measurements->background_q10 - measured.background_q10) < 1e-6 &&
            std::abs(result->measurements->background_q50 - measured.background_q50) < 1e-6 &&
            std::abs(result->measurements->background_q90 - measured.background_q90) < 1e-6,
        "actual Working I01 returns context-admitted converged independent cell observations");
    illumination_working_pixels(fixture, reference, measured, before, *result);
}
} // namespace docenhance::fuzz
