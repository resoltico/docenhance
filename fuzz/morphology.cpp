// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/morphology.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/illumination.hpp"
#include "linear_fixture.hpp"
#include "morphology_reference.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace {
constexpr unsigned dimension_choices = 8;
constexpr unsigned radius_choices = 4;
constexpr double sample_maximum = 255;
constexpr double field_tolerance = 1e-12;
constexpr std::size_t budget_limit = std::size_t{2} * 1024 * 1024;
std::uint8_t byte(std::span<const std::uint8_t> input, std::size_t at) {
    return at < input.size() ? input.subspan(at, 1).front() : 0;
}
void compare(std::span<const std::uint8_t> input) {
    namespace core = docenhance::core;
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    using docenhance::fuzz::require;
    const image::Extent extent{
        .width = 1U + (byte(input, 0) % dimension_choices),
        .height = 1U + (byte(input, 1) % dimension_choices),
    };
    const unsigned radius = 1U + (byte(input, 2) % radius_choices);
    core::Budget budget{budget_limit};
    docenhance::tests::LinearFixture source{budget, extent};
    std::vector<double> values;
    for (unsigned y = 0; y < extent.height; ++y) {
        for (unsigned x = 0; x < extent.width; ++x) {
            const auto value =
                byte(input, 3 + (std::size_t{y} * extent.width) + x) / sample_maximum;
            values.push_back(value);
            std::ranges::fill(
                source.row(y).subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels),
                value);
        }
    }
    const auto reference = docenhance::tests::morphology_reference(
        values, {.width = extent.width, .height = extent.height}, radius);
    const auto entering = budget.used();
    {
        const auto method = methods::Morphology::create({.radius = radius}).value();
        methods::IlluminationReport report;
        const auto model =
            methods::MorphologyModel::prepare({source, {}}, method, budget, {}, report);
        require(model.has_value() && model->active(), "I02 finite input prepares");
        for (unsigned y = 0; y < extent.height; ++y) {
            for (unsigned x = 0; x < extent.width; ++x) {
                const auto value = model->background(x, y);
                require(value.has_value() && std::isfinite(*value), "I02 field is finite");
                require(std::abs(*value - reference[(std::size_t{y} * extent.width) + x]) <
                            field_tolerance,
                        "I02 independent square and Gaussian reference");
            }
        }
    }
    require(budget.used() == entering, "I02 preparation refunds ownership");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    compare({data, size});
    return 0;
}
