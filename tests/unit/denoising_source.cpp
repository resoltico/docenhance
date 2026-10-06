// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "denoising.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "linear_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>
namespace docenhance::tests {
TEST_CASE("Prepared D01 protection and zero corrections preserve exact entering working samples") {
    constexpr std::size_t bytes = std::size_t{1024} * 1024;
    core::Budget budget{bytes};
    constexpr std::uint32_t width = 3;
    constexpr std::uint16_t quantized = 12345;
    const image::Rgb entering{0.123456789, 0.345678912, 0.789123456};
    LinearFixture source{budget, {.width = width, .height = 1}};
    for (std::uint32_t x = 0; x < width; ++x) {
        std::ranges::copy(entering,
                          source.row(0)
                              .subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels)
                              .begin());
    }
    host::NlmPlanes nlm_planes{
        .input = image::Plane<std::uint16_t>::allocate(budget, width, 1).value(),
        .output = image::Plane<std::uint16_t>::allocate(budget, width, 1).value(),
    };
    std::ranges::fill(nlm_planes.input.view().row(0), quantized);
    std::ranges::fill(nlm_planes.output.view().row(0), quantized);
    nlm_planes.output.view().row(0).front() = UINT16_MAX;
    nlm_planes.output.view().row(0).back() = static_cast<std::uint16_t>(quantized + 1);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, width, 1).value();
    std::ranges::fill(mask.view().row(0), 0);
    mask.view().row(0).front() = 1;
    const auto method = methods::Nlm::create().value();
    methods::DenoisingReport report{.requested = method.parameters()};
    const host::DenoisingPlanes planes{std::move(nlm_planes)};
    host::DenoisedSource prepared{source, planes, mask.view().as_const(), report, {}};
    std::array<double, width * image::rgb_channels> output{};
    REQUIRE(prepared.read({.row = 0, .first = 0}, output, image::RowUse::output));
    REQUIRE(std::ranges::equal(std::span{output}.first(image::rgb_channels), entering));
    REQUIRE(std::ranges::equal(std::span{output}.subspan(image::rgb_channels, image::rgb_channels),
                               entering));
    REQUIRE(report.evaluated_samples == 2);
    REQUIRE(report.corrected_samples == 1);
    REQUIRE(report.changed_samples == 1);
    std::array<double, width * image::rgb_channels> original{};
    for (std::uint32_t x = 0; x < width; ++x) {
        std::ranges::copy(entering,
                          std::span{original}
                              .subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels)
                              .begin());
    }
    std::array<std::uint8_t, width * image::rgb_channels> original_bytes{};
    std::array<std::uint8_t, width * image::rgb_channels> output_bytes{};
    const image::RasterShape shape{
        .width = width,
        .height = 1,
        .model = image::SampleModel::rgb,
        .depth = image::SampleDepth::byte(),
    };
    REQUIRE(image::quantize_linear(shape, original, original_bytes));
    REQUIRE(image::quantize_linear(shape, output, output_bytes));
    REQUIRE(original_bytes == output_bytes);
    const auto before = report;
    REQUIRE(prepared.read({.row = 0, .first = 0}, output, image::RowUse::verification));
    REQUIRE(report == before);
}
} // namespace docenhance::tests
