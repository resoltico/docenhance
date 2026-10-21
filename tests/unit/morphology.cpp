// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/morphology.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/morphology_working.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/methods/working.hpp"
#include "morphology_reference.hpp"
#include "morphology_working_detail.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t morphology_budget = std::size_t{2} * 1024 * 1024;
methods::Morphology options(unsigned radius = 1) {
    return methods::Morphology::create({.radius = radius}).value();
}
core::Result<methods::MorphologyFit> fit_field(image::PlaneView<double> source,
                                               image::PlaneView<const std::uint8_t> mask,
                                               const methods::Morphology& method,
                                               core::Budget& budget,
                                               const core::Cancellation& cancellation = {}) {
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(), cancellation};
    const methods::Working working{source, mask, budget, scheduler};
    auto counts = methods::eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    return methods::fit_morphology(method, working, counts->eligible);
}
void compare_field(image::PlaneView<const double> field, image::Extent extent,
                   std::span<const double> reference) {
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const auto b =
            field.row(static_cast<unsigned>(i / extent.width)).subspan(i % extent.width, 1).front();
        REQUIRE(std::isfinite(b));
        CHECK(std::abs(b - reference[i]) < 1e-12);
    }
}
void independent_case(image::Extent extent, unsigned radius) {
    core::Budget budget{morphology_budget};
    auto source = image::Plane<double>::allocate(budget, extent.width, extent.height).value();
    std::vector<double> samples;
    for (unsigned y = 0; y < extent.height; ++y) {
        for (unsigned x = 0; x < extent.width; ++x) {
            constexpr unsigned range = 101;
            const auto value = static_cast<double>(((19 * x) + (37 * y)) % range) / (range - 1);
            samples.push_back(value);
            source.view().row(y).subspan(x, 1).front() = value;
        }
    }
    const auto reference =
        morphology_reference(samples, {.width = extent.width, .height = extent.height}, radius);
    const auto entering_charge = budget.used();
    {
        const auto prepared = fit_field(source.view(), {}, options(radius), budget);
        REQUIRE(prepared);
        compare_field(prepared->background.view(), extent, reference);
    }
    CHECK(budget.used() == entering_charge);
}
} // namespace
TEST_CASE("I02 float64 closing and smoothing agree with independent square and Gaussian references",
          "[morphology]") {
    for (const auto extent : std::to_array<image::Extent>({
             {.width = 1, .height = 1},
             {.width = 1, .height = 9},
             {.width = 7, .height = 1},
             {.width = 3, .height = 5},
             {.width = 7, .height = 9},
         })) {
        for (const auto radius : {1U, 3U, 16U}) {
            independent_case(extent, radius);
        }
    }
    independent_case({.width = 1, .height = 1}, methods::background_radius_domain.maximum);
}
TEST_CASE("I02 admission resolves exact radius and rejects every invalid parameter",
          "[morphology]") {
    CHECK(methods::morphology_radius({.width = 375, .height = 800},
                                     methods::Morphology::create().value()) == 8);
    CHECK(methods::morphology_radius({.width = 425, .height = 800},
                                     methods::Morphology::create().value()) == 9);
    CHECK(methods::morphology_radius({.width = 10000, .height = 10000},
                                     methods::Morphology::create().value()) == 128);
    CHECK(methods::morphology_radius({.width = 1, .height = 1}, options(256)) == 256);
    for (const auto r : {0U, 257U, std::numeric_limits<unsigned>::max()}) {
        CHECK(!methods::Morphology::create({.radius = r}));
    }
    for (const auto v : {-1.0, 1.01, std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(!methods::Morphology::create({.strength = v}));
    }
    CHECK(!methods::Morphology::create({.max_gain = 4.01}));
    CHECK(!methods::Morphology::create({.target = 0.09}));
}
TEST_CASE("I02 protected analysis fill and eager output bypass preserve entering Y",
          "[morphology]") {
    core::Budget budget{morphology_budget};
    constexpr image::Extent extent{.width = 9, .height = 5};
    auto source = image::Plane<double>::allocate(budget, extent.width, extent.height).value();
    std::ranges::fill(source.view().storage(), 0.4);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height).value();
    std::ranges::fill(mask.view().storage(), 0);
    mask.view().row(2).subspan(4, 1).front() = 1;
    source.view().row(2).subspan(4, 1).front() = 0;
    const auto held = budget.used();
    {
        const auto fit = fit_field(source.view(), mask.view().as_const(), options(), budget);
        REQUIRE(fit);
        CHECK(fit->analysis.analysis_fill == 0.4);
        CHECK(fit->background.view().row(2).subspan(4, 1).front() == 0.4);
    }
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto facts =
        methods::apply(options(), {source.view(), mask.view().as_const(), budget, scheduler});
    REQUIRE(facts);
    CHECK(facts->counts.evaluated == 44);
    CHECK(facts->counts.protected_samples == 1);
    CHECK(source.view().row(2).subspan(4, 1).front() == 0);
    CHECK(budget.used() == held);
}
TEST_CASE("I02 no-ops and all-black statistics do not manufacture changes", "[morphology]") {
    core::Budget budget{morphology_budget};
    auto source = image::Plane<double>::allocate(budget, 8, 4).value();
    std::ranges::fill(source.view().storage(), 0);
    const auto held = budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto facts = methods::apply(options(), {source.view(), {}, budget, scheduler});
    REQUIRE(facts);
    REQUIRE(facts->analysis.has_value());
    if (!facts->analysis.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    CHECK(facts->analysis.value().target == 0);
    CHECK(facts->counts.changed == 0);
    CHECK(methods::validate(*facts));
    core::Budget empty{0};
    const auto zero = methods::Morphology::create({.strength = 0, .radius = 256}).value();
    const auto inactive = methods::apply(zero, {source.view(), {}, empty, scheduler});
    REQUIRE(inactive);
    CHECK(inactive->reason == methods::MorphologyReason::zero_strength);
    CHECK(!inactive->analysis);
    CHECK(inactive->counts.evaluated == 0);
    CHECK(empty.used() == 0);
    CHECK(budget.used() == held);
}
TEST_CASE("I02 exact phase budget and every fit checkpoint refund actual ownership",
          "[morphology]") {
    core::Budget input_budget{morphology_budget};
    auto source = image::Plane<double>::allocate(input_budget, 8, 4).value();
    std::ranges::fill(source.view().storage(), 0.4);
    core::Budget measured{morphology_budget};
    {
        const auto fit = fit_field(source.view(), {}, options(), measured);
        REQUIRE(fit);
    }
    CHECK(measured.used() == 0);
    const auto peak = measured.peak();
    REQUIRE(peak > 0);
    core::Budget exact{peak};
    {
        const auto fit = fit_field(source.view(), {}, options(), exact);
        REQUIRE(fit);
    }
    CHECK(exact.used() == 0);
    core::Budget refused{peak - 1};
    const auto refusal = fit_field(source.view(), {}, options(), refused);
    REQUIRE(!refusal);
    CHECK(refusal.error().code == core::ErrorCode::resource);
    CHECK(refused.used() == 0);
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::measurement,
             core::Checkpoint::processing,
         }) {
        std::size_t visits = 0;
        {
            const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
            const auto fit =
                fit_field(source.view(), {}, options(), measured, observe.cancellation());
            REQUIRE(fit);
            visits = CheckpointStop::visits();
        }
        REQUIRE(visits > 0);
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            const CheckpointStop stop{phase, allowed};
            const auto fit = fit_field(source.view(), {}, options(), measured, stop.cancellation());
            REQUIRE(!fit);
            CHECK(fit.error().code == core::ErrorCode::cancelled);
            CHECK(measured.used() == 0);
        }
    }
}
TEST_CASE("I02 protects its kernel storage and admitted-state contracts", "[morphology]") {
    core::Budget budget{morphology_budget};
    auto input = image::Plane<double>::allocate(budget, 3, 2).value();
    auto output = image::Plane<double>::allocate(budget, 3, 2).value();
    std::ranges::fill(input.view().storage(), 0.4);
    std::array<std::uint64_t, 3> queue{};
    CHECK(!methods::extrema_pass(input.view().as_const(), input.view(), queue,
                                 {.radius = 1, .first_line = 0, .last_line = 2}));
    CHECK(!methods::extrema_pass(input.view().as_const(), output.view(), queue,
                                 {.radius = 2, .first_line = 0, .last_line = 2}));
    const std::array<double, 3> weights{0.5, 1, 0.5};
    CHECK(!image::gaussian_pass(
        input.view().as_const(), input.view(),
        {.weights = weights, .normalization = 2, .first_row = 0, .last_row = 2}));
    CHECK(!image::gaussian_pass(
        input.view().as_const(), output.view(),
        {.weights = weights, .normalization = 1, .first_row = 0, .last_row = 2}));
    std::ranges::fill(output.view().row(0), 1.0);
    CHECK(!image::gaussian_pass(
        input.view().as_const(), output.view(),
        {.weights = output.view().row(0), .normalization = 3, .first_row = 0, .last_row = 2}));
    REQUIRE(image::gaussian_pass(
        input.view().as_const(), output.view(),
        {.weights = weights, .normalization = 2, .first_row = 0, .last_row = 2}));
    CHECK(output.view().row(0).front() == 0.4);
}
TEST_CASE("I02 broad dark content stays in the actual field and moved buffers lose ownership",
          "[morphology]") {
    core::Budget budget{morphology_budget};
    auto source = image::Plane<double>::allocate(budget, 17, 17).value();
    std::ranges::fill(source.view().storage(), 0.9);
    for (unsigned y = 4; y <= 12; ++y) {
        std::ranges::fill(source.view().row(y).subspan(4, 9), 0.1);
    }
    const auto held = budget.used();
    {
        auto fit = fit_field(source.view(), {}, options(), budget);
        REQUIRE(fit);
        CHECK(fit->background.view().row(8).subspan(8, 1).front() < 0.11);
        auto moved = std::move(fit->background);
        CHECK(fit->background.empty());
        CHECK(fit->background.width() == 0);
        CHECK(moved.view().row(0).front() > 0.89);
        CHECK(moved.view().row(8).subspan(8, 1).front() < 0.11);
    }
    CHECK(budget.used() == held);
}
TEST_CASE("I02 fallback uses the first eligible point and rejects insufficient eligibility",
          "[morphology]") {
    core::Budget budget{morphology_budget};
    auto source = image::Plane<double>::allocate(budget, 1025, 1).value();
    std::ranges::fill(source.view().storage(), 0.3);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, 1025, 1).value();
    std::ranges::fill(mask.view().storage(), 1);
    for (unsigned x = 1; x < 34; x += 2) {
        mask.view().row(0).subspan(x, 1).front() = 0;
    }
    source.view().row(0).subspan(1, 1).front() = 0.2;
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto facts =
        methods::apply(options(), {source.view(), mask.view().as_const(), budget, scheduler});
    REQUIRE(facts);
    REQUIRE(facts->analysis.has_value());
    if (!facts->analysis.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    CHECK(facts->analysis.value().fallback);
    CHECK(facts->analysis.value().count == 1);
    CHECK(facts->analysis.value().analysis_fill == 0.2);
    mask.view().row(0).subspan(1, 1).front() = 1;
    mask.view().row(0).subspan(3, 1).front() = 1;
    const auto refused =
        methods::apply(options(), {source.view(), mask.view().as_const(), budget, scheduler});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::method_inapplicable);
}
TEST_CASE("Completed I02 gain observations cannot claim application before evaluated samples",
          "[morphology]") {
    core::Budget budget{morphology_budget};
    auto source = image::Plane<double>::allocate(budget, 8, 8).value();
    std::ranges::fill(source.view().storage(), 0.5);
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto facts = methods::apply(options(), {source.view(), {}, budget, scheduler});
    REQUIRE(facts);
    REQUIRE(methods::validate(*facts));
    CHECK(facts->counts.evaluated == 64);
    auto invalid = *facts;
    invalid.min_gain = invalid.max_gain = 2;
    invalid.counts.evaluated = 0;
    CHECK(!methods::validate(invalid));
}
} // namespace docenhance::tests
