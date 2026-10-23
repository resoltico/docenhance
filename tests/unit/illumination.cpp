// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/illumination.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/surface_working.hpp"
#include "docenhance/methods/working.hpp"
#include "surface_detail.hpp"
#include "surface_working_detail.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::tests {
namespace {
constexpr std::size_t surface_budget = std::size_t{32} * 1024 * 1024;
constexpr image::Extent page{.width = 32, .height = 16};
constexpr double paper = 0.5;
constexpr std::uint32_t cell = 8;
methods::Surface options(bool automatic = false) {
    return methods::Surface::create({
                                        .mode = automatic ? methods::SurfaceMode::automatic
                                                          : methods::SurfaceMode::explicit_surface,
                                        .cell = cell,
                                    })
        .value();
}
bool surface_predicate_true(std::optional<bool> value) {
    return value.value_or(false);
}
void paint_dark(image::PlaneView<double> source, bool everywhere) {
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const auto column = x / cell;
            const auto row = y / cell;
            const bool dark = everywhere ? column != 0 : column < 2 && row >= 1 && row < 3;
            source.row(y).subspan(x, 1).front() = dark ? 0.05 : 0.8;
        }
    }
}
core::Result<methods::SurfaceFit> fit_coefficients(const methods::Surface& method,
                                                   methods::Working working) {
    auto counts = methods::eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    const auto grid = methods::surface_grid(
        {.width = working.luminance.width(), .height = working.luminance.height()}, method);
    if (!grid) {
        return std::unexpected(grid.error());
    }
    return methods::fit_surface(method, working, {.counts = *counts}, *grid);
}
methods::SurfacePredicates
contradicted_surface_predicates(methods::SurfaceMeasurements measurements, std::size_t i) {
    auto candidate = measurements;
    double coverage = 1;
    switch (i) {
    case 0:
        coverage = 0;
        break;
    case 1:
        candidate.luminance_q90 = 0;
        break;
    case 2:
        candidate.background_q50 = 0;
        break;
    case 3:
        candidate.variation = 0;
        break;
    case 4:
        candidate.paper_fraction = 0;
        break;
    default:
        candidate.dark_fraction = 0;
        break;
    }
    return methods::surface_decisions(candidate, coverage);
}
} // namespace
TEST_CASE("I01 factory and application admission reject contradictions before I/O",
          "[surface][app]") {
    static_assert(!std::is_default_constructible_v<methods::Surface>);
    auto params = options().parameters();
    params.strength = std::numeric_limits<double>::quiet_NaN();
    CHECK(!methods::Surface::create(params));
    params = options().parameters();
    params.cell = 1;
    CHECK(!methods::Surface::create(params));
    app::Invocation request;
    request.command = app::Command::process;
    request.operands = {"input.png"};
    request.option(app::OptionId::out_dir) = "out";
    request.option(app::OptionId::illumination) = "surface";
    REQUIRE(app::prepare_process(request));
    request.option(app::OptionId::background_cell) = "";
    CHECK(!app::prepare_process(request));
    request.option(app::OptionId::background_cell) = "008";
    REQUIRE(app::prepare_process(request));
    request.option(app::OptionId::illumination) = "off";
    CHECK(!app::prepare_process(request));
    request.option(app::OptionId::background_cell).reset();
    request.option(app::OptionId::protect_mask) = "mask.png";
    REQUIRE(app::prepare_process(request));
    request.option(app::OptionId::output_mode) = "bw";
    const auto binary = app::prepare_process(request);
    REQUIRE(binary);
    CHECK(binary->plan().protection);
    CHECK(binary->mask_path() == "mask.png");
    const auto* const output = std::get_if<methods::BinaryOutput>(&binary->plan().output);
    REQUIRE(output);
    CHECK(std::holds_alternative<methods::Sauvola>(output->method));
    request.option(app::OptionId::binarize) = "fixed";
    const auto fixed = app::prepare_process(request);
    REQUIRE(fixed);
    CHECK(fixed->plan().protection);
    CHECK(fixed->mask_path() == "mask.png");
    const auto* const fixed_output = std::get_if<methods::BinaryOutput>(&fixed->plan().output);
    REQUIRE(fixed_output);
    CHECK(std::holds_alternative<methods::FixedThreshold>(fixed_output->method));
}
TEST_CASE("Disabled algebra and all-protected input avoid fit allocation exactly",
          "[surface][identity]") {
    core::Budget owner{surface_budget};
    auto source = image::Plane<double>::allocate(owner, page.width, page.height).value();
    std::ranges::fill(source.view().storage(), paper);
    auto mask = image::Plane<std::uint8_t>::allocate(owner, page.width, page.height).value();
    std::ranges::fill(mask.view().storage(), std::uint8_t{1});
    core::Budget empty{0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto protected_result =
        methods::apply(options(), {source.view(), mask.view().as_const(), empty, scheduler});
    REQUIRE(protected_result);
    CHECK(protected_result->reason == methods::SurfaceReason::no_eligible_samples);
    CHECK(protected_result->counts.protected_samples == std::uint64_t{page.width} * page.height);
    auto p = options().parameters();
    p.strength = 0;
    const auto zero =
        methods::apply(methods::Surface::create(p).value(), {source.view(), {}, empty, scheduler});
    REQUIRE(zero);
    CHECK(zero->reason == methods::SurfaceReason::zero_strength);
    CHECK(!zero->solver);
    CHECK(!zero->measurements);
    CHECK(source.view().row(0).front() == paper);
    CHECK(empty.used() == 0);
}
TEST_CASE("I01 actual fit interpolation and eager application preserve protected samples",
          "[surface]") {
    core::Budget budget{surface_budget};
    auto source = image::Plane<double>::allocate(budget, page.width, page.height).value();
    std::ranges::fill(source.view().storage(), paper);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, page.width, page.height).value();
    std::ranges::fill(mask.view().storage(), std::uint8_t{0});
    mask.view().row(0).front() = 1;
    source.view().row(0).front() = 0.01;
    auto p = options().parameters();
    p.target = 1;
    p.max_gain = 2;
    const auto method = methods::Surface::create(p).value();
    const auto held = budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    {
        const auto fit =
            fit_coefficients(method, {source.view(), mask.view().as_const(), budget, scheduler});
        REQUIRE(fit);
        REQUIRE(fit->facts.solver.has_value());
        if (!fit->facts.solver.has_value()) {
            throw std::logic_error("Required fitted surface observation is absent");
        }
        CHECK(fit->facts.solver.value().residual <= fit->facts.solver.value().tolerance);
        CHECK(std::abs(methods::surface_background_value(fit->grid, fit->logarithms.view(),
                                                         {.x = 0, .y = 0}) -
                       paper) < 1e-10);
        CHECK(std::abs(
                  methods::surface_background_value(fit->grid, fit->logarithms.view(),
                                                    {.x = page.width - 1, .y = page.height - 1}) -
                  paper) < 1e-10);
    }
    CHECK(budget.used() == held);
    const auto facts =
        methods::apply(method, {source.view(), mask.view().as_const(), budget, scheduler});
    REQUIRE(facts);
    CHECK(facts->counts.protected_samples == 1);
    CHECK(facts->counts.evaluated == 511);
    CHECK(facts->counts.changed == 511);
    CHECK(source.view().row(0).front() == 0.01);
    CHECK(source.view().row(0).subspan(1, 1).front() > paper);
    CHECK(source.view().row(page.height - 1).back() > paper);
    CHECK(methods::validate(*facts, p, page));
    CHECK(budget.used() == held);
}
TEST_CASE("I01 automatic skip and explicit inapplicability are not numerical failure",
          "[surface][auto]") {
    core::Budget budget{surface_budget};
    auto source = image::Plane<double>::allocate(budget, page.width, page.height).value();
    std::ranges::fill(source.view().storage(), paper);
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto skipped = methods::apply(options(true), {source.view(), {}, budget, scheduler});
    REQUIRE(skipped);
    CHECK(skipped->disposition == methods::Disposition::skipped);
    CHECK(skipped->reason == methods::SurfaceReason::automatic_predicates);
    CHECK(skipped->auto_predicates.background_variation == false);
    std::ranges::fill(source.view().storage(), 0);
    const auto refused = methods::apply(options(), {source.view(), {}, budget, scheduler});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::method_inapplicable);
    const auto black = methods::apply(options(true), {source.view(), {}, budget, scheduler});
    REQUIRE(black);
    CHECK(black->disposition == methods::Disposition::skipped);
}
TEST_CASE("I01 resource refusal and actual phase cancellation refund scratch",
          "[surface][resource]") {
    core::Budget owner{surface_budget};
    auto source = image::Plane<double>::allocate(owner, page.width, page.height).value();
    std::ranges::fill(source.view().storage(), paper);
    core::Budget empty{0};
    const exec::Scheduler healthy{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto refused = methods::apply(options(), {source.view(), {}, empty, healthy});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::resource);
    CHECK(empty.used() == 0);
    for (const auto phase :
         {core::Checkpoint::measurement, core::Checkpoint::solving, core::Checkpoint::processing}) {
        core::Budget scratch{surface_budget};
        const CheckpointStop stop{phase, 0};
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                        stop.cancellation()};
        const auto result = methods::apply(options(), {source.view(), {}, scratch, scheduler});
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(source.view().row(0).front() == paper);
        CHECK(scratch.used() == 0);
    }
}
TEST_CASE("I01 automatic predicates each retain their independent decision", "[surface][auto]") {
    const methods::SurfaceMeasurements measurements{
        .background_q50 = 0.7,
        .luminance_q90 = 0.8,
        .variation = 0.2,
        .paper_fraction = 0.8,
        .dark_fraction = 0.1,
    };
    const auto healthy = methods::surface_decisions(measurements, 1);
    REQUIRE(std::ranges::all_of(healthy.values(), surface_predicate_true));
    for (std::size_t i = 0; i < methods::surface_predicates; ++i) {
        const auto predicates = contradicted_surface_predicates(measurements, i);
        CHECK(predicates.values().at(i) == false);
        CHECK(!std::ranges::all_of(predicates.values(), surface_predicate_true));
    }
}
TEST_CASE("I01 exact measured eager budget succeeds and one byte short refunds",
          "[surface][resources]") {
    core::Budget owner{surface_budget};
    auto source = image::Plane<double>::allocate(owner, page.width, page.height).value();
    std::ranges::fill(source.view().storage(), paper);
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    core::Budget measured{surface_budget};
    REQUIRE(methods::apply(options(), {source.view(), {}, measured, scheduler}));
    CHECK(measured.used() == 0);
    const auto required = measured.peak();
    REQUIRE(required > 0);
    core::Budget exact{required};
    REQUIRE(methods::apply(options(), {source.view(), {}, exact, scheduler}));
    CHECK(exact.used() == 0);
    core::Budget short_budget{required - 1};
    const auto refused = methods::apply(options(), {source.view(), {}, short_budget, scheduler});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::resource);
    CHECK(short_budget.used() == 0);
    CHECK(source.view().row(0).front() == paper);
}
TEST_CASE("I01 singleton and protected lattice retain defined actual measurements",
          "[surface][statistics]") {
    core::Budget budget{surface_budget};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    {
        auto source = image::Plane<double>::allocate(budget, 1, 1).value();
        source.view().row(0).front() = paper;
        const auto facts = methods::apply(options(), {source.view(), {}, budget, scheduler});
        REQUIRE(facts);
        REQUIRE(facts->measurements.has_value());
        if (!facts->measurements.has_value()) {
            throw std::logic_error("Required fitted surface observation is absent");
        }
        CHECK(facts->measurements.value().count == 1);
        CHECK(facts->measurements.value().background_q90 == paper);
    }
    auto source = image::Plane<double>::allocate(budget, 1025, 1).value();
    std::ranges::fill(source.view().storage(), paper);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, 1025, 1).value();
    for (std::uint32_t x = 0; x < 1025; ++x) {
        mask.view().row(0).subspan(x, 1).front() = static_cast<std::uint8_t>(x % 2 == 0);
    }
    auto p = options().parameters();
    p.cell = 32;
    const auto facts = methods::apply(methods::Surface::create(p).value(),
                                      {source.view(), mask.view().as_const(), budget, scheduler});
    REQUIRE(facts);
    REQUIRE(facts->measurements.has_value());
    if (!facts->measurements.has_value()) {
        throw std::logic_error("Required fitted surface observation is absent");
    }
    CHECK(facts->measurements.value().fallback);
    CHECK(facts->measurements.value().stride == 2);
    CHECK(facts->measurements.value().count == 512);
}
TEST_CASE("Solid dark content stays unmeasured and actual coefficients span surrounding paper",
          "[surface][dark]") {
    core::Budget budget{surface_budget};
    auto source = image::Plane<double>::allocate(budget, 48, 32).value();
    paint_dark(source.view(), false);
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    {
        const auto fit = fit_coefficients(options(), {source.view(), {}, budget, scheduler});
        REQUIRE(fit);
        CHECK(fit->facts.dark_cells == 4);
        CHECK(fit->facts.measured_cells == fit->facts.cells - 4);
        REQUIRE(fit->facts.background_reference);
        if (!fit->facts.background_reference.has_value()) {
            throw std::logic_error("Required fitted background reference is absent");
        }
        CHECK(std::abs(*fit->facts.background_reference - 0.8) < 1e-6);
        CHECK(std::abs(methods::surface_background_value(fit->grid, fit->logarithms.view(),
                                                         {.x = cell, .y = 2 * cell}) -
                       0.8) < 1e-6);
    }
    paint_dark(source.view(), true);
    const auto refused = fit_coefficients(options(), {source.view(), {}, budget, scheduler});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::method_inapplicable);
    const auto skipped = methods::apply(options(true), {source.view(), {}, budget, scheduler});
    REQUIRE(skipped);
    CHECK(skipped->dark_cells == 20);
}
TEST_CASE("Moving actual fitted coefficient storage empties its owner without stale borrowing",
          "[surface]") {
    core::Budget budget{surface_budget};
    auto source = image::Plane<double>::allocate(budget, page.width, page.height).value();
    std::ranges::fill(source.view().storage(), paper);
    const auto held = budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    {
        auto fit = fit_coefficients(options(), {source.view(), {}, budget, scheduler});
        REQUIRE(fit);
        const auto background = methods::surface_background_value(
            fit->grid, fit->logarithms.view().as_const(), {.x = 0, .y = 0});
        auto retained = std::move(fit->logarithms);
        CHECK(fit->logarithms.empty());
        CHECK(fit->logarithms.width() == 0);
        CHECK(fit->logarithms.height() == 0);
        CHECK(methods::surface_background_value(fit->grid, retained.view().as_const(),
                                                {.x = 0, .y = 0}) == background);
        fit->logarithms = std::move(retained);
        // NOLINTNEXTLINE(bugprone-use-after-move)
        CHECK(retained.empty());
        CHECK(methods::surface_background_value(fit->grid, fit->logarithms.view().as_const(),
                                                {.x = 0, .y = 0}) == background);
    }
    CHECK(budget.used() == held);
}
} // namespace docenhance::tests
