// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/gamma.hpp"
#include "gamma_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace docenhance::tests {
namespace {
void prime_gamma_planes(image::PlaneView<double> luminance, image::PlaneView<std::uint8_t> mask) {
    std::ranges::fill(luminance.storage(), -2);
    for (std::uint32_t y = 0; y < 2; ++y) {
        std::ranges::fill(luminance.row(y), 0.25);
    }
    std::ranges::fill(mask.storage(), 255);
    for (std::uint32_t y = 0; y < 2; ++y) {
        std::ranges::fill(mask.row(y), 0);
    }
    mask.row(0).front() = 1;
}
double healthy_wide_gamma(image::PlaneView<double> luminance,
                          image::PlaneView<const std::uint8_t> mask, core::Budget& budget,
                          const methods::Gamma& method) {
    const auto width = luminance.width();
    const exec::Scheduler healthy{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto completed = methods::apply(method, {luminance, mask, budget, healthy});
    REQUIRE(completed);
    CHECK(completed->counts.changed == (2 * width) - 1);
    const auto healthy_sample = luminance.row(0).subspan(1024, 1).front();
    REQUIRE(healthy_sample != 0.25);
    return healthy_sample;
}
void check_gamma_prefix(image::PlaneView<const double> luminance, double healthy_sample) {
    CHECK(luminance.row(0).front() == 0.25);
    CHECK(luminance.row(0).subspan(1, 1).front() == healthy_sample);
    CHECK(luminance.row(0).subspan(1023, 1).front() == healthy_sample);
    CHECK(luminance.row(0).subspan(1024, 1).front() == 0.25);
    CHECK(luminance.row(0).back() == 0.25);
    CHECK(std::ranges::all_of(luminance.row(1), [](double value) { return value == 0.25; }));
}
void check_gamma_padding(image::PlaneView<const double> luminance,
                         const std::vector<std::uint64_t>& before,
                         image::PlaneView<const std::uint8_t> mask,
                         const std::vector<std::uint8_t>& original_mask) {
    const auto width = luminance.width();
    const auto pitch = luminance.shape().stride / sizeof(double);
    REQUIRE(pitch > width);
    CHECK(std::ranges::equal(gamma_bits(luminance.storage().subspan(width, pitch - width)),
                             std::span{before}.subspan(width, pitch - width)));
    CHECK(std::ranges::equal(original_mask, mask.storage()));
}
} // namespace
TEST_CASE("A wide-row eager stop retains attained stores and padding but returns no Facts") {
    constexpr auto width = (3 * image::linear_block_pixels) + 1;
    core::Budget budget{std::size_t{1024} * 1024};
    {
        auto luminance = image::Plane<double>::allocate(budget, width, 2).value();
        auto mask = image::Plane<std::uint8_t>::allocate(budget, width, 2).value();
        prime_gamma_planes(luminance.view(), mask.view());
        const auto before = gamma_bits(luminance.view().storage());
        const auto mask_bytes = mask.view().storage();
        const std::vector<std::uint8_t> original_mask(mask_bytes.begin(), mask_bytes.end());
        const auto held = budget.used();
        const auto method = methods::Gamma::create({.gamma = 2}).value();
        const auto healthy_sample =
            healthy_wide_gamma(luminance.view(), mask.view().as_const(), budget, method);
        for (std::uint32_t y = 0; y < 2; ++y) {
            std::ranges::fill(luminance.view().row(y), 0.25);
        }
        const CheckpointStop stop{core::Checkpoint::processing, 1};
        const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                      stop.cancellation()};
        const auto partial =
            methods::apply(method, {luminance.view(), mask.view().as_const(), budget, stopped});
        REQUIRE(!partial);
        CHECK(partial.error().code == core::ErrorCode::cancelled);
        CHECK(CheckpointStop::visits() == 2);
        check_gamma_prefix(luminance.view().as_const(), healthy_sample);
        check_gamma_padding(luminance.view().as_const(), before, mask.view().as_const(),
                            original_mask);
        CHECK(budget.used() == held);
    }
    CHECK(budget.used() == 0);
}
} // namespace docenhance::tests
