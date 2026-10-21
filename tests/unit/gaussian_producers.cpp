// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "morphology_reference.hpp"
#include "morphology_working_detail.hpp"
#include "require.hpp"
#include "sharpening_cases.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <vector>

namespace docenhance::tests {
TEST_CASE("Gaussian I02 full-field context includes finite off-lattice samples") {
    constexpr std::size_t limit = std::size_t{2} * 1024 * 1024;
    core::Budget source_budget{limit};
    auto source = image::Plane<double>::allocate(source_budget, 1025, 1);
    REQUIRE(source);
    core::Budget budget{limit};
    const auto method = methods::Morphology::create({.radius = 1});
    REQUIRE(method);
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    for (const bool off_lattice : {false, true}) {
        std::ranges::fill(source->view().row(0), .5);
        if (off_lattice) {
            source->view().row(0).subspan(1, 1).front() = .75;
        }
        const std::vector<double> before(source->view().row(0).begin(),
                                         source->view().row(0).end());
        const auto expected = morphology_reference(before, {.width = 1025, .height = 1}, 1);
        {
            const auto fit =
                methods::fit_morphology(*method, {source->view(), {}, budget, scheduler}, 1025);
            REQUIRE(fit);
            CHECK(fit->analysis.stride == 2);
            CHECK(fit->analysis.count == 513);
            CHECK(!fit->analysis.fallback);
            CHECK(fit->analysis.analysis_fill == .5);
            CHECK(budget.used() != 0);
            for (std::size_t x = 0; x < expected.size(); ++x) {
                CHECK(std::abs(fit->background.view().row(0).subspan(x, 1).front() -
                               expected.at(x)) < 1e-12);
            }
            CHECK((fit->background.view().row(0).subspan(1, 1).front() > .5) == off_lattice);
            CHECK(std::ranges::equal(source->view().row(0), before));
        }
        CHECK(budget.used() == 0);
    }
}
TEST_CASE(
    "Standalone eager sharpening retains its independent two dimensional Gaussian reference") {
    const auto before = assertion_counter().load();
    sharpening_cases();
    REQUIRE(assertion_counter().load() > before);
}
TEST_CASE("Unsharp raw candidates preserve finite excursions and exact soft thresholds") {
    sharpen_threshold_cases();
    const auto method = methods::Unsharp::create({.amount = 2, .threshold = 0});
    REQUIRE(method);
    const auto low = methods::unsharp_candidate(0, 1, *method);
    const auto high = methods::unsharp_candidate(1, 0, *method);
    CHECK(std::isfinite(low));
    CHECK(std::isfinite(high));
    CHECK(low == -2);
    CHECK(high == 3);
}
TEST_CASE("Gaussian ordered accumulation remains unit for nonconstant tiny coefficient context") {
    core::Budget budget{1024};
    auto input = image::Plane<double>::allocate(budget, 3, 1);
    auto output = image::Plane<double>::allocate(budget, 3, 1);
    REQUIRE(input);
    REQUIRE(output);
    constexpr double tiny = 0x1p-53;
    const std::array<double, 3> weights{tiny, 1, tiny};
    input->view().row(0).front() = 1;
    input->view().row(0).subspan(1, 1).front() = 1;
    input->view().row(0).back() = 1 - tiny;
    CHECK(input->view().row(0).front() != input->view().row(0).back());
    REQUIRE(image::gaussian_pass(
        input->view().as_const(), output->view(),
        {.weights = weights, .normalization = 1, .first_row = 0, .last_row = 1}));
    CHECK(output->view().row(0).subspan(1, 1).front() == 1);
}
} // namespace docenhance::tests
