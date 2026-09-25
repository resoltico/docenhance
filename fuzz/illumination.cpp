// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/illumination.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/surface.hpp"
#include "illumination_reference.hpp"
#include "linear_fixture.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace {
constexpr std::uint32_t cell = 8, dimension_choices = 9;
constexpr std::size_t budget_limit = std::size_t{4} * 1024 * 1024;
constexpr double scale = 255, tolerance = 1e-6;
std::uint8_t byte_at(std::span<const std::uint8_t> bytes, std::size_t i) {
    return i < bytes.size() ? bytes.subspan(i, 1).front() : 0;
}
void fill(docenhance::tests::LinearFixture& source, docenhance::image::PlaneView<std::uint8_t> mask,
          std::span<const std::uint8_t> bytes) {
    namespace image = docenhance::image;
    constexpr unsigned bright_floor = 128;
    constexpr unsigned bright_choices = 127;
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t x = 0; x < source.extent().width; ++x) {
            const auto v = byte_at(bytes, 2 + (std::size_t{y} * source.extent().width) + x);
            const double value = (bright_floor + (v % bright_choices)) / scale;
            std::ranges::fill(
                source.row(y).subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels),
                value);
            mask.row(y).subspan(x, 1).front() =
                static_cast<std::uint8_t>(v % cell == 0 && x % cell == 0);
        }
    }
}
void compare_pixels(docenhance::tests::LinearFixture& source,
                    docenhance::image::PlaneView<const std::uint8_t> mask,
                    const docenhance::methods::SurfaceModel& model,
                    const docenhance::tests::LogReference& reference, double target) {
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    using docenhance::fuzz::require;
    constexpr double floor = 0.02;
    methods::IlluminationReport report;
    const auto method = methods::Surface::create({.cell = cell}).value();
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        const auto input = source.row(y);
        std::vector<double> output(input.begin(), input.end());
        require(model.apply({.row = y}, output, mask, report, {}).has_value(),
                "valid I01 application");
        for (std::uint32_t x = 0; x < source.extent().width; ++x) {
            const auto at = std::size_t{x} * image::rgb_channels;
            const double value = input.subspan(at, 1).front();
            const bool protected_pixel = mask.row(y).subspan(x, 1).front() != 0;
            const double gain = std::clamp(target / std::max(reference.background(x, y), floor),
                                           1.0, method.parameters().max_gain);
            const double expected =
                protected_pixel
                    ? value
                    : std::min(1.0, value * std::pow(gain, method.parameters().strength));
            require(std::abs(output.at(at) - expected) < tolerance, "direct I01 sample");
            require(!protected_pixel || output.at(at) == value, "protected I01 identity");
        }
    }
    std::uint64_t expected_count = 0;
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        expected_count += static_cast<std::uint64_t>(std::ranges::count(mask.row(y), 0));
    }
    require(report.evaluated_samples == expected_count, "eligible application counted once");
}
void compare(docenhance::tests::LinearFixture& source,
             docenhance::image::PlaneView<const std::uint8_t> mask,
             docenhance::core::Budget& budget) {
    namespace methods = docenhance::methods;
    using docenhance::fuzz::require;
    const auto method = methods::Surface::create({.cell = cell}).value();
    methods::IlluminationReport report;
    auto model = methods::SurfaceModel::prepare({.source = source, .protection = mask}, method,
                                                budget, {}, report);
    require(model.has_value() && model->active(), "bounded valid I01 fit");
    const auto reference = docenhance::tests::illumination_reference(source, mask, cell);
    std::vector<double> backgrounds;
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t x = 0; x < source.extent().width; ++x) {
            require(std::abs(model->background(x, y).value() - reference.background(x, y)) <
                        tolerance,
                    "direct I01 field");
            if (mask.row(y).subspan(x, 1).front() == 0) {
                backgrounds.push_back(reference.background(x, y));
            }
        }
    }
    constexpr double rank = 0.9;
    const double target = docenhance::tests::sorted_rank(backgrounds, rank);
    compare_pixels(source, mask, *model, reference, target);
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace core = docenhance::core;
    namespace image = docenhance::image;
    const std::span<const std::uint8_t> bytes{data, size};
    const image::Extent extent{
        .width = cell + (byte_at(bytes, 0) % dimension_choices),
        .height = cell + (byte_at(bytes, 1) % dimension_choices),
    };
    core::Budget budget{budget_limit};
    docenhance::tests::LinearFixture source{budget, extent};
    auto mask = image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height).value();
    fill(source, mask.view(), bytes);
    const auto held = budget.used();
    compare(source, mask.view().as_const(), budget);
    docenhance::fuzz::require(budget.used() == held, "I01 fit scratch refunded");
    return 0;
}
