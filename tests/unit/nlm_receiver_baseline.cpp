// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_output.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "gamma_fixture.hpp"
#include "nlm_source_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>
namespace docenhance::tests {
namespace {
void verify_nlm_output(NlmSourceFixture& fixture) {
    auto pixels = image::Plane<std::uint8_t>::allocate(fixture.budget, 32, 32).value();
    for (std::uint32_t y = 0; y < 32; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            pixels.view().row(y).subspan(x, 1).front() = static_cast<std::uint8_t>(std::floor(
                (255 * std::clamp(image::srgb_encode(
                                      fixture.luminance.view().row(y).subspan(x, 1).front()),
                                  0.0, 1.0)) +
                0.5));
        }
    }
    const auto canonical = color::make_output_profile(fixture.budget, true).value();
    auto profile = image::Plane<std::uint8_t>::allocate(
                       fixture.budget, static_cast<std::uint32_t>(canonical.size()), 1)
                       .value();
    std::ranges::transform(canonical.bytes(), profile.view().row(0).begin(),
                           [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
    const codec::PngImage image{
        .shape = {.width = 32, .height = 32, .depth = image::PngDepth()},
        .pixels = pixels.view().as_const(),
        .profile = std::as_bytes(profile.view().row(0)),
        .resolution = std::nullopt,
    };
    const auto encoded = codec::encode_png(image, fixture.budget);
    REQUIRE(encoded);
    REQUIRE(codec::compare_png(encoded->bytes(), image, fixture.budget));
    REQUIRE(codec::compare_png(encoded->bytes(), image, fixture.budget));
    auto damaged = fixture.budget.allocate(encoded->bytes().size()).value();
    std::ranges::copy(encoded->bytes(), damaged.bytes().begin());
    damaged.bytes().back() ^= std::byte{1};
    const auto refused = codec::compare_png(damaged.bytes(), image, fixture.budget);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::output_verify);
}
} // namespace
TEST_CASE("NLM immutable blend admission precedes eager application and preserves owned samples") {
    core::Budget budget{nlm_source_budget};
    {
        NlmSourceFixture fixture{budget};
        REQUIRE(extract_nlm_source(fixture));
        const auto initial = gamma_bits(fixture.luminance.view().storage());
        const auto held = budget.used();
        const auto healthy = apply_nlm_fixture(fixture, {.h = 0.1, .patch = 3, .search = 7});
        REQUIRE(healthy);
        CHECK(healthy->counts.evaluated == 1023);
        CHECK(healthy->corrected_samples == 0);
        CHECK(gamma_bits(fixture.luminance.view().storage()) == initial);
        for (const auto blend : {
                 -0.01,
                 1.01,
                 std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::quiet_NaN(),
             }) {
            const auto invalid = methods::Nlm::create({.blend = blend});
            REQUIRE(!invalid);
            CHECK(invalid.error().code == core::ErrorCode::argument);
            CHECK(gamma_bits(fixture.luminance.view().storage()) == initial);
            CHECK(budget.used() == held);
        }
        CHECK(healthy->counts.changed == 0);
        CHECK(budget.used() == held);
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("Eager NLM observes pending cancellation without returning fictional completed Facts") {
    core::Budget budget{nlm_source_budget};
    {
        const std::stop_source stop;
        NlmSourceFixture fixture{budget, image::SampleModel::rgb,
                                 core::Cancellation{stop.get_token()}};
        REQUIRE(extract_nlm_source(fixture));
        const auto initial = gamma_bits(fixture.luminance.view().storage());
        const auto held = budget.used();
        static_cast<void>(stop.request_stop());
        const auto stopped = apply_nlm_fixture(fixture, {.h = 0.1, .patch = 3, .search = 7});
        REQUIRE(!stopped);
        CHECK(stopped.error().code == core::ErrorCode::cancelled);
        CHECK(gamma_bits(fixture.luminance.view().storage()) == initial);
        CHECK(budget.used() == held);
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("Eager NLM genuine correction and independent verification retain owned Facts") {
    core::Budget budget{nlm_source_budget};
    {
        NlmSourceFixture fixture{budget};
        REQUIRE(extract_nlm_source(fixture));
        const auto original_source = fixture.source.pixels.view().storage();
        const std::vector<std::uint8_t> source_bytes(original_source.begin(),
                                                     original_source.end());
        fixture.luminance.view().row(0).subspan(1, 1).front() += 0.001;
        const auto entering = fixture.luminance.view().row(0).subspan(1, 1).front();
        const auto protected_bits = gamma_bits(fixture.luminance.view().row(0).first(1));
        const auto descriptor = fixture.interpreter->descriptor();
        const auto held = budget.used();
        const auto facts = apply_nlm_fixture(fixture, {.h = 3, .patch = 3, .search = 7});
        REQUIRE(facts);
        REQUIRE(facts->corrected_samples > 0);
        REQUIRE(facts->counts.changed > 0);
        REQUIRE(fixture.luminance.view().row(0).subspan(1, 1).front() != entering);
        CHECK(gamma_bits(fixture.luminance.view().row(0).first(1)) == protected_bits);
        CHECK(std::ranges::equal(source_bytes, original_source));
        CHECK(budget.used() == held);
        const auto frozen = *facts;
        const auto final_samples = gamma_bits(fixture.luminance.view().storage());
        verify_nlm_output(fixture);
        CHECK(*facts == frozen);
        CHECK(gamma_bits(fixture.luminance.view().storage()) == final_samples);
        CHECK(fixture.interpreter->descriptor() == descriptor);
        CHECK(std::ranges::equal(source_bytes, original_source));
        CHECK(budget.used() == held);
    }
    CHECK(budget.used() == 0);
}
} // namespace docenhance::tests
