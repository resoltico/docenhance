// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/color/converter.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/surface.hpp"
#include "linear_rows.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace docenhance::tests {
namespace {
core::Result<host::IlluminationModel>
prepare_model(color::Converter& converter, image::PlaneView<const std::uint8_t> protection,
              core::Budget& budget, methods::IlluminationReport& report, bool morphological) {
    if (morphological) {
        auto model = methods::MorphologyModel::prepare(
            {converter, protection},
            methods::Morphology::create({.target = 1, .radius = 1}).value(), budget, {}, report);
        if (!model) {
            return std::unexpected(model.error());
        }
        return host::IlluminationModel{std::move(*model)};
    }
    auto model = methods::SurfaceModel::prepare(
        {converter, protection}, methods::Surface::create({.target = 1, .cell = 8}).value(), budget,
        {}, report);
    if (!model) {
        return std::unexpected(model.error());
    }
    return host::IlluminationModel{std::move(*model)};
}
} // namespace

TEST_CASE("Illumination completion requires one complete output traversal", "[host][surface]") {
    constexpr std::uint32_t width = 16;
    constexpr std::uint32_t height = 2;
    core::Budget budget{std::size_t{1024} * 1024};
    image::Raster raster;
    raster.shape = {.width = width, .height = height};
    raster.pixels = image::Plane<std::uint8_t>::allocate(budget, width, height).value();
    for (std::uint32_t y = 0; y < height; ++y) {
        std::ranges::fill(raster.pixels.view().row(y), 128);
    }
    auto converter =
        color::Converter::create(raster, image::Continuous::create({}).value(), budget);
    REQUIRE(converter);
    auto protection = image::Plane<std::uint8_t>::allocate(budget, width, height).value();
    for (std::uint32_t y = 0; y < height; ++y) {
        std::ranges::fill(protection.view().row(y), 0);
    }
    protection.view().row(height - 1).back() = 1;
    const bool morphological = GENERATE(false, true);
    methods::IlluminationReport report;
    const auto retained =
        prepare_model(**converter, protection.view().as_const(), budget, report, morphological);
    REQUIRE(retained);
    host::IlluminatedSource source{
        **converter, &*retained, protection.view().as_const(), report, {}};
    std::array<double, std::size_t{width} * image::rgb_channels> rgb{};
    const auto half = std::span{rgb}.first((std::size_t{width} / 2) * image::rgb_channels);
    CHECK(!source.read({.row = height - 1, .first = width / 2}, half, image::RowUse::output));
    CHECK(!report.complete);
    CHECK(report.evaluated_samples == 0);
    REQUIRE(source.read({.row = 0, .first = 0}, half, image::RowUse::output));
    CHECK(!report.complete);
    const auto evaluated = report.evaluated_samples;
    CHECK(!source.read({.row = 0, .first = 0}, half, image::RowUse::output));
    CHECK(!source.read({.row = 1, .first = 0}, half, image::RowUse::output));
    CHECK(report.evaluated_samples == evaluated);
    REQUIRE(
        source.read({.row = height - 1, .first = width / 2}, half, image::RowUse::verification));
    CHECK(!report.complete);
    CHECK(report.evaluated_samples == evaluated);
    REQUIRE(source.read({.row = 0, .first = width / 2}, half, image::RowUse::output));
    REQUIRE(source.read({.row = 1, .first = 0}, rgb, image::RowUse::output));
    CHECK(report.complete);
    CHECK(report.evaluated_samples == (width * height) - 1);
    const auto changed = report.changed_samples;
    REQUIRE(source.read({.row = 0, .first = 0}, rgb, image::RowUse::verification));
    CHECK(report.evaluated_samples == (width * height) - 1);
    CHECK(report.changed_samples == changed);
    CHECK(!source.read({.row = 0, .first = 0}, rgb, image::RowUse::output));
    CHECK(report.complete);
}
} // namespace docenhance::tests
