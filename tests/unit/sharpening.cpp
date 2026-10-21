// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/sharpening.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/unsharp.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "sharpen_detail.hpp"
#include "sharpen_reference.hpp"
#include "unsharp_working_controls.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t limit = std::size_t{2} * 1024 * 1024;
void populate(image::PlaneView<double> source, const std::vector<double>& values) {
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            source.row(y).subspan(x, 1).front() =
                image::srgb_decode(values.at((std::size_t{y} * source.width()) + x));
        }
    }
}
void check_sharpened_row(image::PlaneView<const double> source, image::Extent extent,
                         const std::vector<double>& values, const std::vector<double>& expected,
                         std::uint32_t y) {
    for (std::uint32_t x = 0; x < extent.width; ++x) {
        const auto at = (std::size_t{y} * extent.width) + x;
        const auto linear = sharpen_reference_linear_output(
            sharpen_reference_srgb_decode(values.at(at)), values.at(at), expected.at(at));
        CHECK(std::isfinite(source.row(y).subspan(x, 1).front()));
        CHECK(std::abs(source.row(y).subspan(x, 1).front() - linear) < 2e-12);
    }
}
void check_sharpened_pixels(image::PlaneView<const double> source,
                            const std::vector<double>& values,
                            const std::vector<double>& expected) {
    const image::Extent extent{.width = source.width(), .height = source.height()};
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        check_sharpened_row(source, extent, values, expected, y);
    }
}
void check_sharpening_facts(const methods::UnsharpFacts& facts, const std::vector<double>& values,
                            const std::vector<double>& expected, const core::Budget& budget,
                            std::size_t held) {
    CHECK(facts.counts.evaluated == values.size());
    REQUIRE(facts.pre_clamp.has_value());
    if (!facts.pre_clamp.has_value()) {
        throw std::logic_error("Required unsharp candidate range is absent");
    }
    CHECK(std::abs(facts.pre_clamp.value().low - *std::ranges::min_element(expected)) < 1e-12);
    CHECK(std::abs(facts.pre_clamp.value().high - *std::ranges::max_element(expected)) < 1e-12);
    CHECK(budget.used() == held);
}
void check_reference_case(image::Extent extent, double sigma) {
    core::Budget budget{limit};
    auto source = image::Plane<double>::allocate(budget, extent.width, extent.height).value();
    std::ranges::fill(source.view().storage(), -2);
    std::vector<double> values(std::size_t{extent.width} * extent.height, 0.25);
    values.front() = 0.9;
    values.back() = 0.1;
    populate(source.view(), values);
    const auto method =
        methods::Unsharp::create({.sigma = sigma, .amount = 0.5, .threshold = 0}).value();
    const auto expected = sharpen_reference(values, {
                                                        .width = extent.width,
                                                        .height = extent.height,
                                                        .sigma = sigma,
                                                        .amount = 0.5,
                                                        .threshold = 0,
                                                    });
    const auto held = budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto facts = methods::apply(method, {source.view(), {}, budget, scheduler});
    REQUIRE(facts);
    check_sharpened_pixels(source.view().as_const(), values, expected);
    check_sharpening_facts(*facts, values, expected, budget, held);
}
} // namespace
TEST_CASE("Independent sharpening oracle preserves sRGB knot and detects every RGB mutation",
          "[sharpening]") {
    // The published piecewise sRGB constants have a tiny forward/inverse discontinuity.
    // The old encode(actual) == raw candidate check would reject a correct linear output.
    constexpr double encoded_knot_neighbor = 0.0404499671;
    const auto expected = sharpen_reference_srgb_decode(encoded_knot_neighbor);
    CHECK(std::abs(sharpen_reference_srgb_encode(expected) - encoded_knot_neighbor) > 2e-12);
    CHECK(sharpen_reference_linear_output(0.25, encoded_knot_neighbor, encoded_knot_neighbor) ==
          0.25);
    CHECK(sharpen_reference_linear_output(0.25, encoded_knot_neighbor,
                                          encoded_knot_neighbor + 0.01) != 0.25);
    for (const auto encoded : std::array{0.0, 0.0404499671, 0.04045, 0.04045001, 0.5, 1.0}) {
        const auto linear = sharpen_reference_srgb_decode(encoded);
        std::array<double, image::rgb_channels> channels{linear, linear, linear};
        CHECK(sharpen_reference_gray_rgb_matches(channels, linear));
        for (auto& component : channels) {
            component += 1e-5;
            CHECK(!sharpen_reference_gray_rgb_matches(channels, linear));
            component -= 1e-5;
        }
    }
}

TEST_CASE("Unsharp soft threshold uses signed residual and public byte-equivalent units",
          "[sharpening]") {
    const auto method =
        methods::Unsharp::create({.sigma = 0.8, .amount = 2, .threshold = 2.55}).value();
    CHECK(std::abs(methods::unsharp_candidate(0.5, 0.48, method) - 0.52) < 1e-14);
    CHECK(std::abs(methods::unsharp_candidate(0.5, 0.52, method) - 0.48) < 1e-14);
    CHECK(methods::unsharp_candidate(0.5, 0.5, method) == 0.5);
    const auto equality = methods::Unsharp::create({.amount = 2, .threshold = 15.9375}).value();
    for (const auto blurred : std::array{0.4375, 0.5625}) {
        CHECK(methods::unsharp_candidate(0.5, blurred, equality) == 0.5);
        const auto farther = std::nextafter(blurred, blurred < 0.5 ? 0.0 : 1.0);
        const auto closer = std::nextafter(blurred, 0.5);
        CHECK(methods::unsharp_candidate(0.5, closer, equality) == 0.5);
        CHECK(methods::unsharp_candidate(0.5, farther, equality) != 0.5);
    }
    const auto endpoints = methods::Unsharp::create({.amount = 1, .threshold = 0}).value();
    CHECK(methods::unsharp_candidate(0.25, 0.5, endpoints) == 0);
    CHECK(methods::unsharp_candidate(0.75, 0.5, endpoints) == 1);
    CHECK(!methods::Unsharp::create({.sigma = 0.29}));
    CHECK(!methods::Unsharp::create({.amount = 2.01}));
    CHECK(!methods::Unsharp::create({.threshold = -1}));
    CHECK(!methods::Unsharp::create({.sigma = std::numeric_limits<double>::infinity()}));
}
TEST_CASE("Unsharp impulses and borders agree with direct two-dimensional Gaussian reference",
          "[sharpening]") {
    for (const auto extent : std::array{
             image::Extent{.width = 1, .height = 1},
             image::Extent{.width = 1, .height = 9},
             image::Extent{.width = 9, .height = 1},
             image::Extent{.width = 7, .height = 5},
         }) {
        for (const auto sigma : std::array{0.3, 0.8, 3.0}) {
            check_reference_case(extent, sigma);
        }
    }
}
TEST_CASE("Unsharp constants and protected destinations remain exact while protection is context",
          "[sharpening]") {
    core::Budget budget{limit};
    auto source = image::Plane<double>::allocate(budget, 9, 3).value();
    std::ranges::fill(source.view().storage(), 0.3125);
    const auto method = methods::Unsharp::create({.sigma = 3, .amount = 2, .threshold = 0}).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto identity = methods::apply(method, {source.view(), {}, budget, scheduler});
    REQUIRE(identity);
    CHECK(std::ranges::all_of(source.view().row(1), [](double x) { return x == 0.3125; }));
    auto mask = image::Plane<std::uint8_t>::allocate(budget, 9, 3).value();
    std::ranges::fill(mask.view().storage(), 0);
    mask.view().row(1).subspan(4, 1).front() = 1;
    std::vector<double> values(27, 0.5);
    values.at(13) = 1;
    populate(source.view(), values);
    const auto before = source.view().row(1).subspan(4, 1).front();
    const auto facts =
        methods::apply(method, {source.view(), mask.view().as_const(), budget, scheduler});
    REQUIRE(facts);
    CHECK(source.view().row(1).subspan(4, 1).front() == before);
    CHECK(source.view().row(1).subspan(3, 1).front() < image::srgb_decode(0.5));
    CHECK(facts->context_samples == 27);
    CHECK(facts->counts.evaluated == 26);
}
TEST_CASE("Unsharp exact charged fields refund at the actual eager apply boundary",
          "[sharpening][memory]") {
    core::Budget owner{limit};
    auto source = image::Plane<double>::allocate(owner, 7, 5).value();
    std::ranges::fill(source.view().storage(), 0.4);
    const auto shape = image::plane_shape(7, 5, sizeof(double)).value();
    const auto required = 2 * image::plane_bytes(shape).value();
    const auto method = methods::Unsharp::create().value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto before = gamma_bits(source.view().storage());
    for (const auto bytes : std::array{required - 1, required}) {
        core::Budget scratch{bytes};
        const auto facts = methods::apply(method, {source.view(), {}, scratch, scheduler});
        CHECK(facts.has_value() == (bytes == required));
        if (!facts) {
            CHECK(facts.error().code == core::ErrorCode::resource);
        }
        CHECK(scratch.used() == 0);
        CHECK(gamma_bits(source.view().storage()) == before);
    }
    core::Budget empty{0};
    const auto noop = methods::apply(methods::Unsharp::create({.amount = 0}).value(),
                                     {source.view(), {}, empty, scheduler});
    REQUIRE(noop);
    CHECK(noop->reason == methods::UnsharpReason::zero_amount);
    CHECK(noop->counts.evaluated == 0);
    CHECK(empty.used() == 0);
}
TEST_CASE("Unsharp every eager phase checkpoint cancels and refunds deterministically",
          "[sharpening][cancel]") {
    core::Budget owner{limit};
    auto source = image::Plane<double>::allocate(owner, 257, 3).value();
    std::ranges::fill(source.view().storage(), 0.4);
    const auto method = methods::Unsharp::create().value();
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::measurement,
             core::Checkpoint::processing,
         }) {
        bool completed = false;
        for (std::size_t allowed = 0; allowed < 64 && !completed; ++allowed) {
            core::Budget scratch{limit};
            const CheckpointStop stop{phase, allowed};
            const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                            stop.cancellation()};
            const auto result = methods::apply(method, {source.view(), {}, scratch, scheduler});
            if (result) {
                CHECK(!CheckpointStop::stopped());
                completed = true;
            } else {
                CHECK(result.error().code == core::ErrorCode::cancelled);
                CHECK(CheckpointStop::stopped());
            }
            CHECK(scratch.used() == 0);
        }
        CHECK(completed);
    }
}
TEST_CASE("Unsharp actual bounded map stops preserve attained stores and protected prefixes",
          "[sharpening][cancel]") {
    const auto before = assertion_counter().load();
    unsharp_map_stop_case(false);
    unsharp_map_stop_case(true);
    REQUIRE(assertion_counter().load() > before);
}
TEST_CASE("Unsharp real field ownership and fixed worker partitions preserve immutable input",
          "[sharpening]") {
    core::Budget budget{limit};
    auto first = image::Plane<double>::allocate(budget, 11, 3).value();
    auto second = image::Plane<double>::allocate(budget, 11, 3).value();
    std::ranges::fill(first.view().storage(), -2);
    std::vector<double> values(33);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values.at(i) = 0.15 + (static_cast<double>((i * 7) % 23) / 40);
    }
    populate(first.view(), values);
    std::ranges::copy(first.view().storage(), second.view().storage().begin());
    const auto entering = gamma_bits(first.view().storage());
    const auto method = methods::Unsharp::create({.threshold = 0}).value();
    const exec::Scheduler one{exec::Concurrency::resolve(1, 3, 0, 0).value()};
    const exec::Scheduler three{exec::Concurrency::resolve(3, 3, 0, 0).value()};
    const auto held = budget.used();
    {
        auto field = methods::fit_unsharp(method, {first.view(), {}, budget, one});
        REQUIRE(field);
        const auto view = field->view();
        const auto pitch = view.shape().stride / sizeof(double);
        for (std::size_t y = 0; y < view.height(); ++y) {
            std::ranges::fill(
                view.storage().subspan((y * pitch) + view.width(), pitch - view.width()), -3.0);
        }
        const auto snapshot = gamma_bits(field->view().storage());
        for (const auto y : {2U, 0U, 1U}) {
            CHECK(!field->view().row(y).empty());
        }
        CHECK(gamma_bits(field->view().storage()) == snapshot);
        CHECK(gamma_bits(first.view().storage()) == entering);
        const auto moved = std::move(*field);
        CHECK(field->empty());
        CHECK(!moved.empty());
    }
    CHECK(budget.used() == held);
    const auto a = methods::apply(method, {first.view(), {}, budget, one});
    const auto b = methods::apply(method, {second.view(), {}, budget, three});
    REQUIRE(a);
    REQUIRE(b);
    CHECK(*a == *b);
    CHECK(gamma_bits(first.view().storage()) == gamma_bits(second.view().storage()));
    auto wrong = image::Plane<std::uint8_t>::allocate(budget, 10, 3).value();
    const auto before = gamma_bits(first.view().storage());
    const auto refused =
        methods::apply(method, {first.view(), wrong.view().as_const(), budget, one});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::argument);
    CHECK(gamma_bits(first.view().storage()) == before);
    CHECK(!image::PlaneView<double>::create(
        first.view().storage(), {.width = 11, .height = 4, .stride = first.view().shape().stride}));
    CHECK(!image::PlaneView<double>::create(
        first.view().storage(), {.width = 12, .height = 3, .stride = 11 * sizeof(double)}));
}
TEST_CASE("Unsharp completed Facts enforce requested excursion and correction bounds",
          "[sharpening]") {
    const methods::UnsharpParameters parameters{.amount = 0.01, .threshold = 0};
    methods::UnsharpFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::UnsharpReason::none,
        .counts = {.eligible = 2, .evaluated = 2, .changed = 2},
        .context_samples = 2,
        .corrected_samples = 2,
        .pre_clamp = methods::ExcursionRange{.low = 0, .high = 1},
    };
    REQUIRE(methods::validate(facts, parameters));
    CHECK(facts.clipped_low_samples == 0);
    CHECK(facts.clipped_high_samples == 0);
    facts.pre_clamp = methods::ExcursionRange{.low = -0.01, .high = 1.01};
    facts.clipped_low_samples = facts.clipped_high_samples = 1;
    REQUIRE(methods::validate(facts, parameters));
    auto invalid = facts;
    invalid.corrected_samples = invalid.counts.changed = 1;
    CHECK(!methods::validate(invalid, parameters));
    invalid = facts;
    invalid.pre_clamp = methods::ExcursionRange{.low = std::nextafter(-0.01, -1.0), .high = 1.01};
    CHECK(!methods::validate(invalid, parameters));
    invalid = facts;
    invalid.pre_clamp = methods::ExcursionRange{.low = -0.01, .high = std::nextafter(1.01, 2.0)};
    CHECK(!methods::validate(invalid, parameters));
    invalid = facts;
    invalid.pre_clamp = methods::ExcursionRange{.low = 0, .high = 1.01};
    CHECK(!methods::validate(invalid, parameters));
    invalid = facts;
    invalid.pre_clamp = methods::ExcursionRange{.low = -0.01, .high = 1};
    CHECK(!methods::validate(invalid, parameters));
}
} // namespace docenhance::tests
