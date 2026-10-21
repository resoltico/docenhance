// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/morphology.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/morphology_working.hpp"
#include "docenhance/methods/parameters.hpp"
#include "morphology_reference.hpp"
#include "morphology_working_detail.hpp"
#include "morphology_working_fixture.hpp"
#include "morphology_working_reference.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

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
void compare_morphology_field(const docenhance::methods::MorphologyFit& fit,
                              const docenhance::tests::MorphologyWorkingFixture& fixture,
                              std::span<const double> values, unsigned radius) {
    using docenhance::fuzz::require;
    const docenhance::image::Extent extent{
        .width = fixture.luminance.width(),
        .height = fixture.luminance.height(),
    };
    const auto reference = docenhance::tests::morphology_reference(
        values, {.width = extent.width, .height = extent.height}, radius);
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const auto value = fit.background.view().row(y).subspan(x, 1).front();
            require(std::isfinite(value) &&
                        std::abs(value - reference.at((std::size_t{y} * extent.width) + x)) <
                            field_tolerance,
                    "I02 independent square/Gaussian phase field");
            require(fixture.luminance.view().row(y).subspan(x, 1).front() ==
                        values[(std::size_t{y} * extent.width) + x],
                    "I02 fit leaves source Y exact");
        }
    }
}
void compare_working(std::span<const double> values, docenhance::image::Extent extent,
                     unsigned radius) {
    namespace methods = docenhance::methods;
    using docenhance::fuzz::require;
    docenhance::tests::MorphologyWorkingFixture fixture{extent.width, extent.height, budget_limit};
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        const auto row = fixture.luminance.view().row(y);
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            row.subspan(x, 1).front() = values[(std::size_t{y} * extent.width) + x];
        }
    }
    const methods::MorphologyParameters p{.radius = radius};
    const auto expected = docenhance::tests::morphology_working_reference(fixture, p);
    const auto resident = fixture.budget.used();
    const auto concurrency = docenhance::exec::Concurrency::resolve(1, 1, 0, 0);
    require(concurrency.has_value(), "I02 scheduler admits");
    const docenhance::exec::Scheduler scheduler{*concurrency};
    const auto method = methods::Morphology::create(p);
    require(method.has_value(), "I02 parameters admit");
    {
        const auto fit = methods::fit_morphology(
            *method, {fixture.luminance.view(), {}, fixture.budget, scheduler}, values.size());
        require(fit.has_value(), "I02 finite field fits");
        compare_morphology_field(*fit, fixture, values, radius);
        require(fixture.budget.used() > resident, "I02 field retained charge");
    }
    require(fixture.budget.used() == resident, "I02 fit owner refunds");
    const auto actual =
        methods::apply(*method, {fixture.luminance.view(), fixture.protection.view().as_const(),
                                 fixture.budget, scheduler});
    require(
        actual && methods::validate(*actual, p, extent) &&
            actual->counts == expected.facts.counts &&
            actual->gain_capped_samples == expected.facts.gain_capped_samples &&
            actual->saturated_samples == expected.facts.saturated_samples,
        "Working I02 PixelCounts/capped/saturated match oracle; parameter/extent Facts validate");
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        const auto row = fixture.luminance.view().row(y);
        for (const auto value : row) {
            require(std::isfinite(value) && value >= 0 && value <= 1,
                    "every logical Working I02 result is finite Unit before oracle comparison");
        }
    }
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < expected.samples.size(); ++i) {
        require(std::isfinite(output.subspan(i, 1).front()) &&
                    std::abs(output.subspan(i, 1).front() - expected.samples.at(i)) <
                        field_tolerance,
                "actual Working I02 linear target agrees with independent square/Gaussian/gain "
                "reference");
    }
    require(fixture.budget.used() == resident, "actual Working I02 refunds every phase owner");
}
void compare(std::span<const std::uint8_t> input) {
    namespace image = docenhance::image;
    const image::Extent extent{
        .width = 1U + (byte(input, 0) % dimension_choices),
        .height = 1U + (byte(input, 1) % dimension_choices),
    };
    const unsigned radius = 1U + (byte(input, 2) % radius_choices);
    std::vector<double> values;
    for (unsigned y = 0; y < extent.height; ++y) {
        for (unsigned x = 0; x < extent.width; ++x) {
            const auto value =
                byte(input, 3 + (std::size_t{y} * extent.width) + x) / sample_maximum;
            values.push_back(value);
        }
    }
    compare_working(values, extent, radius);
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    compare({data, size});
    return 0;
}
