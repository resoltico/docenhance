// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/working.hpp"
#include "gamma_fixture.hpp"
#include "surface_detail.hpp"
#include "surface_working_detail.hpp"
#include "surface_working_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <numbers>
namespace docenhance::tests {
namespace {
enum class SurfaceRawCategory { overflow, underflow, above_unit, unit };
struct SurfaceProducerCase {
    double logarithm;
    SurfaceRawCategory category;
};
bool surface_raw_category(double raw, SurfaceRawCategory expected) {
    switch (expected) {
    case SurfaceRawCategory::overflow:
        return std::isinf(raw) && raw > 0;
    case SurfaceRawCategory::underflow:
        return raw == 0;
    case SurfaceRawCategory::above_unit:
        return std::isfinite(raw) && raw > 1;
    case SurfaceRawCategory::unit:
        return std::isfinite(raw) && raw > 0 && raw < 1;
    }
    return false;
}
} // namespace
TEST_CASE("Working surface private coefficient admission earns finite rather than Unit logs") {
    SurfaceWorkingFixture fixture{8, 8, surface_test_scratch};
    auto logs = image::Plane<double>::allocate(fixture.budget, 1, 1).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const methods::Working working{fixture.luminance.view(), fixture.protection.view().as_const(),
                                   fixture.budget, scheduler};
    for (const auto value : {1.0, -1000.0, 1000.0}) {
        logs.view().row(0).front() = value;
        REQUIRE(methods::admit_surface_coefficients(working, logs.view().as_const()));
    }
    for (const auto value : {
             std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity(),
         }) {
        logs.view().row(0).front() = value;
        const auto failure = methods::admit_surface_coefficients(working, logs.view().as_const());
        REQUIRE(!failure);
        REQUIRE(failure.error().code == core::ErrorCode::numerical);
        REQUIRE(failure.error().message == "Nonfinite fitted log surface");
    }
}
TEST_CASE("Working surface private producers admit new background before target and skip protected "
          "destinations") {
    SurfaceWorkingFixture fixture{8, 8, surface_test_scratch};
    auto logs = image::Plane<double>::allocate(fixture.budget, 1, 1).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const methods::Working working{fixture.luminance.view(), fixture.protection.view().as_const(),
                                   fixture.budget, scheduler};
    const auto grid = methods::surface_grid({.width = 8, .height = 8},
                                            methods::Surface::create({.cell = 8}).value())
                          .value();
    const auto held = fixture.budget.used();
    const auto before = gamma_bits(fixture.luminance.view().storage());
    // Fixture categories are independent declared binary64/RN exp expectations, never
    // classifications learned from the production output being tested.
    constexpr std::array cases{
        SurfaceProducerCase{.logarithm = 1000.0, .category = SurfaceRawCategory::overflow},
        SurfaceProducerCase{.logarithm = -1000.0, .category = SurfaceRawCategory::underflow},
        SurfaceProducerCase{
            .logarithm = std::numbers::ln2,
            .category = SurfaceRawCategory::above_unit,
        },
        SurfaceProducerCase{.logarithm = -std::numbers::ln2, .category = SurfaceRawCategory::unit},
    };
    for (const auto sample : cases) {
        // Finite coefficients control actual producer mechanisms; this is no fitted-image claim.
        logs.view().row(0).front() = sample.logarithm;
        const auto raw =
            methods::surface_background_value(grid, logs.view().as_const(), {.x = 0, .y = 0});
        REQUIRE(surface_raw_category(raw, sample.category));
        const bool positive = sample.category == SurfaceRawCategory::above_unit ||
                              sample.category == SurfaceRawCategory::unit;
        const auto measured =
            methods::measure_surface_working(working, grid, logs.view().as_const(), 64);
        REQUIRE(measured.has_value() == positive);
        if (positive) {
            REQUIRE(measured->count == 64);
            REQUIRE(measured->background_q10 == raw);
            REQUIRE(measured->background_q50 == raw);
            REQUIRE(measured->background_q90 == raw);
            REQUIRE(measured->target == raw);
        } else {
            REQUIRE(measured.error().code == core::ErrorCode::numerical);
            REQUIRE(measured.error().message == "Invalid interpolated log surface");
        }
        const methods::SurfaceMapInput input{
            .field = {.grid = grid, .logarithms = logs.view().as_const()},
            .gain =
                {
                    .target = std::numeric_limits<double>::quiet_NaN(),
                    .strength = 1,
                    .max_gain = 2,
                },
            .target_valid = false,
        };
        const auto mapped = methods::map_surface_rows(working, input, 0, 8);
        REQUIRE(!mapped);
        REQUIRE(mapped.error().code ==
                (positive ? core::ErrorCode::argument : core::ErrorCode::numerical));
        REQUIRE(mapped.error().message == (positive ? "Invalid illumination application values"
                                                    : "Invalid interpolated log surface"));
        const methods::SurfaceMapInput valid_target{
            .field = {.grid = grid, .logarithms = logs.view().as_const()},
            .gain =
                {
                    .target = 0.1,
                    .strength = 1,
                    .max_gain = 2,
                },
            .target_valid = true,
        };
        const auto finite_target = methods::map_surface_rows(working, valid_target, 0, 8);
        if (sample.category == SurfaceRawCategory::unit) {
            REQUIRE(finite_target);
            REQUIRE(finite_target->changed == 0);
        } else {
            REQUIRE(!finite_target);
            REQUIRE(finite_target.error().code ==
                    (positive ? core::ErrorCode::argument : core::ErrorCode::numerical));
        }
        REQUIRE(gamma_bits(fixture.luminance.view().storage()) == before);
        REQUIRE(fixture.budget.used() == held);
        std::ranges::fill(fixture.protection.view().storage(), 255);
        const auto protected_map = methods::map_surface_rows(working, input, 0, 8);
        REQUIRE(protected_map);
        REQUIRE(protected_map->evaluated == 0);
        REQUIRE(gamma_bits(fixture.luminance.view().storage()) == before);
        for (unsigned y = 0; y < 8; ++y) {
            std::ranges::fill(fixture.protection.view().row(y), 0);
        }
    }
}
} // namespace docenhance::tests
