// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/sharpening.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "linear_fixture.hpp"
#include "sharpen_reference.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t limit = std::size_t{2} * 1024 * 1024;
void populate(LinearFixture& source, const std::vector<double>& values) {
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t x = 0; x < source.extent().width; ++x) {
            std::ranges::fill(
                source.row(y).subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels),
                image::srgb_decode(values[(std::size_t{y} * source.extent().width) + x]).value());
        }
    }
}
void check_reference_row(std::span<const double> row, std::span<const double> entering,
                         std::span<const double> expected) {
    for (std::size_t x = 0; x < expected.size(); ++x) {
        const auto linear = sharpen_reference_linear_output(
            sharpen_reference_srgb_decode(entering[x]), entering[x], expected[x]);
        CHECK(sharpen_reference_gray_rgb_matches(
            row.subspan(x * image::rgb_channels, image::rgb_channels), linear));
    }
}
void check_reference_case(image::Extent extent, double sigma) {
    core::Budget budget{limit};
    LinearFixture source{budget, extent};
    std::vector<double> values(std::size_t{extent.width} * extent.height, 0.25);
    values.front() = 0.9;
    values.back() = 0.1;
    populate(source, values);
    const auto method =
        methods::Unsharp::create({.sigma = sigma, .amount = 0.5, .threshold = 0}).value();
    const auto expected = sharpen_reference(values, {
                                                        .width = extent.width,
                                                        .height = extent.height,
                                                        .sigma = sigma,
                                                        .amount = 0.5,
                                                        .threshold = 0,
                                                    });
    methods::SharpenReport report;
    auto model = methods::SharpenModel::prepare(source, {}, method,
                                                {budget, {}, report, image::RowUse::output});
    REQUIRE(model);
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        const auto row = source.row(y);
        REQUIRE(model->apply({.row = y, .first = 0}, row, {}, report, {}));
        check_reference_row(
            row,
            std::span<const double>{values}.subspan(std::size_t{y} * extent.width, extent.width),
            std::span<const double>{expected}.subspan(std::size_t{y} * extent.width, extent.width));
    }
    CHECK(report.evaluated_samples == values.size());
    const auto range = report.pre_clamp.value_or(methods::ExcursionRange{
        .low = std::numeric_limits<double>::quiet_NaN(),
        .high = std::numeric_limits<double>::quiet_NaN(),
    });
    CHECK(std::abs(range.low - *std::ranges::min_element(expected)) < 1e-12);
    CHECK(std::abs(range.high - *std::ranges::max_element(expected)) < 1e-12);
}
bool check_preparation_cancel(LinearFixture& source, const methods::Unsharp& method,
                              core::Checkpoint phase, std::size_t allowed) {
    core::Budget budget{limit};
    methods::SharpenReport report;
    const CheckpointStop stop{phase, allowed};
    bool completed = false;
    bool cancelled = false;
    {
        auto model = methods::SharpenModel::prepare(
            source, {}, method, {budget, stop.cancellation(), report, image::RowUse::output});
        completed = model.has_value();
        cancelled = model || model.error().code == core::ErrorCode::cancelled;
    }
    CHECK(cancelled);
    CHECK(budget.used() == 0);
    return completed;
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
    CHECK(sharpen_reference_linear_output(0.25, encoded_knot_neighbor, encoded_knot_neighbor + 0.01) !=
          0.25);
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
    CHECK(std::abs(methods::unsharp_candidate(0.5, 0.48, method).value() - 0.52) < 1e-14);
    CHECK(std::abs(methods::unsharp_candidate(0.5, 0.52, method).value() - 0.48) < 1e-14);
    CHECK(methods::unsharp_candidate(0.5, 0.5, method).value() == 0.5);
    const auto equality = methods::Unsharp::create({.amount = 2, .threshold = 15.9375}).value();
    for (const auto blurred : std::array{0.4375, 0.5625}) {
        CHECK(methods::unsharp_candidate(0.5, blurred, equality).value() == 0.5);
        const auto farther = std::nextafter(blurred, blurred < 0.5 ? 0.0 : 1.0);
        const auto closer = std::nextafter(blurred, 0.5);
        CHECK(methods::unsharp_candidate(0.5, closer, equality).value() == 0.5);
        CHECK(methods::unsharp_candidate(0.5, farther, equality).value() != 0.5);
    }
    const auto endpoints = methods::Unsharp::create({.amount = 1, .threshold = 0}).value();
    CHECK(methods::unsharp_candidate(0.25, 0.5, endpoints).value() == 0);
    CHECK(methods::unsharp_candidate(0.75, 0.5, endpoints).value() == 1);
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
TEST_CASE(
    "Unsharp constants and protected destinations remain exact while protection is blur context",
    "[sharpening]") {
    core::Budget budget{limit};
    LinearFixture source{budget, {.width = 9, .height = 3}};
    source.fill(0.3125);
    methods::SharpenReport flat;
    const auto method = methods::Unsharp::create({.sigma = 3, .amount = 2, .threshold = 0}).value();
    auto identity = methods::SharpenModel::prepare(source, {}, method,
                                                   {budget, {}, flat, image::RowUse::output});
    REQUIRE(identity);
    REQUIRE(identity->apply({.row = 1, .first = 0}, source.row(1), {}, flat, {}));
    CHECK(std::ranges::all_of(source.row(1), [](double x) { return x == 0.3125; }));
    auto mask = image::Plane<std::uint8_t>::allocate(budget, 9, 3).value();
    std::ranges::fill(mask.view().storage(), 0);
    mask.view().row(1)[4] = 1;
    std::vector<double> values(27, 0.5);
    values[13] = 1;
    populate(source, values);
    methods::SharpenReport protected_report;
    auto model =
        methods::SharpenModel::prepare(source, mask.view().as_const(), method,
                                       {budget, {}, protected_report, image::RowUse::output});
    REQUIRE(model);
    const auto before = source.row(1)[12];
    REQUIRE(model->apply({.row = 1, .first = 0}, source.row(1), mask.view().as_const(),
                         protected_report, {}));
    CHECK(source.row(1)[12] == before);
    CHECK(source.row(1)[9] < image::srgb_decode(0.5).value());
    CHECK(protected_report.context_samples == 27);
    CHECK(protected_report.evaluated_samples == 8);
}
TEST_CASE("Unsharp exact charged budget, refusal and immutable field refund are observable",
          "[sharpening][memory]") {
    core::Budget source_budget{limit};
    LinearFixture source{source_budget, {.width = 7, .height = 5}};
    source.fill(0.4);
    const auto shape = image::plane_shape(7, 5, sizeof(double)).value();
    const auto required = (2 * image::plane_bytes(shape).value()) +
                          (image::linear_block_pixels * image::rgb_channels * sizeof(double));
    const auto method = methods::Unsharp::create().value();
    for (const auto bytes : std::array{required - 1, required}) {
        core::Budget budget{bytes};
        methods::SharpenReport report;
        {
            auto model = methods::SharpenModel::prepare(
                source, {}, method, {budget, {}, report, image::RowUse::output});
            CHECK(model.has_value() == (bytes == required));
            if (model) {
                CHECK(budget.used() == image::plane_bytes(shape).value());
            } else {
                CHECK(model.error().code == core::ErrorCode::resource);
            }
        }
        CHECK(budget.used() == 0);
    }
    core::Budget empty{0};
    methods::SharpenReport report;
    auto noop =
        methods::SharpenModel::prepare(source, {}, methods::Unsharp::create({.amount = 0}).value(),
                                       {empty, {}, report, image::RowUse::output});
    REQUIRE(noop);
    CHECK(!noop->active());
    CHECK(report.preparation_charge_peak == 0);
}
TEST_CASE("Unsharp preparation and reconstruction checkpoints cancel deterministically",
          "[sharpening][cancel]") {
    core::Budget source_budget{limit};
    LinearFixture source{source_budget, {.width = 257, .height = 3}};
    source.fill(0.4);
    const auto method = methods::Unsharp::create().value();
    for (const auto phase : std::array{
             core::Checkpoint::allocation,
             core::Checkpoint::measurement,
             core::Checkpoint::processing,
         }) {
        bool completed = false;
        for (std::size_t n = 0; n < 64 && !completed; ++n) {
            completed = check_preparation_cancel(source, method, phase, n);
        }
        CHECK(completed);
    }
}
TEST_CASE("Unsharp reconstruction checkpoints cover each bounded interval",
          "[sharpening][cancel]") {
    core::Budget source_budget{limit};
    LinearFixture source{source_budget, {.width = 257, .height = 3}};
    source.fill(0.4);
    const auto method = methods::Unsharp::create().value();
    core::Budget budget{limit};
    methods::SharpenReport report;
    auto model = methods::SharpenModel::prepare(source, {}, method,
                                                {budget, {}, report, image::RowUse::output});
    REQUIRE(model);
    const auto prepared = report;
    for (std::size_t n = 0; n <= 3; ++n) {
        auto observed = prepared;
        std::vector<double> row(source.row(0).begin(), source.row(0).end());
        const CheckpointStop stop{core::Checkpoint::processing, n};
        const auto applied =
            model->apply({.row = 0, .first = 0}, row, {}, observed, stop.cancellation());
        CHECK(applied.has_value() == (n == 3));
        if (!applied) {
            CHECK(applied.error().code == core::ErrorCode::cancelled);
            CHECK(observed.evaluated_samples == 128 * n);
        } else {
            CHECK(observed.evaluated_samples == 257);
        }
    }
}
TEST_CASE("Unsharp reconstruction is immutable across interior split and reordered blocks",
          "[sharpening]") {
    core::Budget budget{limit};
    LinearFixture source{budget, {.width = 11, .height = 3}};
    std::vector<double> values(33);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = 0.15 + (static_cast<double>((i * 7) % 23) / 40);
    }
    populate(source, values);
    const auto method = methods::Unsharp::create({.threshold = 0}).value();
    methods::SharpenReport preparation;
    auto model = methods::SharpenModel::prepare(source, {}, method,
                                                {budget, {}, preparation, image::RowUse::output});
    REQUIRE(model);
    for (const auto y : std::array<std::uint32_t, 3>{2, 0, 1}) {
        const std::vector<double> before(source.row(y).begin(), source.row(y).end());
        auto whole = before;
        auto split = before;
        auto full_report = preparation;
        auto split_report = preparation;
        REQUIRE(model->apply({.row = y}, whole, {}, full_report, {}));
        for (const auto& block :
             std::array{std::pair{4U, 3U}, std::pair{7U, 4U}, std::pair{0U, 4U}}) {
            REQUIRE(model->apply({.row = y, .first = block.first},
                                 std::span<double>{split}.subspan(std::size_t{3} * block.first,
                                                                  std::size_t{3} * block.second),
                                 {}, split_report, {}));
        }
        CHECK(split == whole);
        CHECK(full_report == split_report);
        CHECK(std::ranges::equal(source.row(y), before));
        auto ignored = preparation;
        CHECK(!model->apply({.row = 3}, whole, {}, ignored, {}));
        CHECK(!model->apply({.row = y, .first = 11}, whole, {}, ignored, {}));
        CHECK(!model->apply({.row = y, .first = 1}, whole, {}, ignored, {}));
        CHECK(!model->apply({.row = y}, {}, {}, ignored, {}));
        CHECK(!model->apply({.row = y}, std::span<double>{whole}.first(2), {}, ignored, {}));
        auto wrong_mask = image::Plane<std::uint8_t>::allocate(budget, 10, 3).value();
        CHECK(!model->apply({.row = y}, whole, wrong_mask.view().as_const(), ignored, {}));
        CHECK(ignored == preparation);
    }
}
TEST_CASE("Unsharp observation validation enforces requested excursion and correction bounds",
          "[sharpening]") {
    methods::SharpenReport report;
    report.requested = methods::UnsharpParameters{.amount = 0.01, .threshold = 0};
    report.status = methods::SharpenStatus::applied;
    report.eligible_samples = report.context_samples = report.evaluated_samples = 2;
    report.corrected_samples = report.changed_samples = 2;
    report.pre_clamp = methods::ExcursionRange{.low = 0, .high = 1};
    REQUIRE(methods::valid_sharpen_observations(report));
    CHECK(report.clipped_low_samples == 0);
    CHECK(report.clipped_high_samples == 0);
    report.pre_clamp = methods::ExcursionRange{.low = -0.01, .high = 1.01};
    report.clipped_low_samples = report.clipped_high_samples = 1;
    REQUIRE(methods::valid_sharpen_observations(report));
    auto invalid = report;
    invalid.corrected_samples = invalid.changed_samples = 1;
    CHECK(!methods::valid_sharpen_observations(invalid));
    invalid = report;
    invalid.pre_clamp = methods::ExcursionRange{.low = std::nextafter(-0.01, -1.0), .high = 1.01};
    CHECK(!methods::valid_sharpen_observations(invalid));
    invalid = report;
    invalid.pre_clamp = methods::ExcursionRange{.low = -0.01, .high = std::nextafter(1.01, 2.0)};
    CHECK(!methods::valid_sharpen_observations(invalid));
    invalid = report;
    invalid.pre_clamp = methods::ExcursionRange{.low = 0, .high = 1.01};
    CHECK(!methods::valid_sharpen_observations(invalid));
    invalid = report;
    invalid.pre_clamp = methods::ExcursionRange{.low = -0.01, .high = 1};
    CHECK(!methods::valid_sharpen_observations(invalid));
}
} // namespace docenhance::tests
