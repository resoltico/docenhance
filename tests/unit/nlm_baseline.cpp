// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "gamma_fixture.hpp"
#include "nlm_source_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stop_token>
#include <type_traits>
#include <vector>
namespace docenhance::tests {
namespace {
double expected_word_luminance(image::SampleModel model) {
    const double encoded = 32768.0 / 65535;
    const double decoded = std::pow((encoded + 0.055) / 1.055, 2.4);
    const double value =
        model == image::SampleModel::rgba ? (encoded * decoded) + (1 - encoded) : decoded;
    return model == image::SampleModel::gray
               ? value
               : (0.2126 * value) + (0.7152 * value) + (0.0722 * value);
}
} // namespace
static_assert(!std::is_copy_constructible_v<color::Interpretation>);
static_assert(!std::is_move_constructible_v<color::Interpretation>);
static_assert(!std::is_copy_constructible_v<NlmSourceFixture>);
static_assert(!std::is_move_constructible_v<NlmSourceFixture>);
TEST_CASE("NLM real gray and RGB extraction owns observations and eager work refunds scratch") {
    for (const auto model :
         {image::SampleModel::gray, image::SampleModel::rgb, image::SampleModel::rgba}) {
        core::Budget budget{nlm_source_budget};
        {
            NlmSourceFixture fixture{budget, model};
            const auto descriptor = fixture.interpreter->descriptor();
            const auto held = budget.used();
            const auto read = extract_nlm_source(fixture);
            REQUIRE(read);
            CHECK(read->flattened_pixels ==
                  (model == image::SampleModel::rgba ? std::uint64_t{1024} : std::uint64_t{0}));
            CHECK(descriptor.flattened_pixels == 0);
            CHECK(read->clipped_components == 0);
            CHECK(fixture.luminance.view().row(0).front() == expected_word_luminance(model));
            CHECK(fixture.interpreter->category() == (model == image::SampleModel::gray
                                                          ? image::InterpretationCategory::gray1
                                                          : image::InterpretationCategory::color3));
            const auto original = gamma_bits(fixture.luminance.view().storage());
            const auto repeated = extract_nlm_source(fixture);
            REQUIRE(repeated);
            CHECK(*repeated == *read);
            CHECK(fixture.interpreter->descriptor() == descriptor);
            CHECK(gamma_bits(fixture.luminance.view().storage()) == original);
            CHECK(budget.used() == held);
            const auto facts = apply_nlm_fixture(fixture, {.h = 0.1, .patch = 3, .search = 7});
            REQUIRE(facts);
            CHECK(facts->counts.eligible == 1023);
            CHECK(facts->counts.protected_samples == 1);
            CHECK(facts->counts.evaluated == 1023);
            CHECK(facts->corrected_samples == 0);
            CHECK(facts->counts.changed == 0);
            CHECK(facts->weight_table_entries > 0);
            CHECK(gamma_bits(fixture.luminance.view().storage()) == original);
            CHECK(budget.used() == held);
        }
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("Actual interpretation source failures and pending stop preserve private Y and owners") {
    for (const auto x : {0U, 1U}) {
        for (const auto channel : {0U, 1U, 2U}) {
            core::Budget budget{nlm_source_budget};
            {
                NlmSourceFixture fixture{budget,
                                         image::SampleModel::rgba,
                                         {},
                                         NlmAssociatedFault{.x = x, .channel = channel}};
                const auto before = gamma_bits(fixture.luminance.view().storage());
                const auto mask = fixture.protection.view().storage();
                const std::vector<std::uint8_t> original_mask(mask.begin(), mask.end());
                const auto descriptor = fixture.interpreter->descriptor();
                const auto held = budget.used();
                const auto read = extract_nlm_source(fixture);
                REQUIRE(!read);
                CHECK(read.error().code == core::ErrorCode::input);
                CHECK(read.error().message == "Associated TIFF color exceeds alpha");
                CHECK(gamma_bits(fixture.luminance.view().storage()) == before);
                CHECK(std::ranges::equal(original_mask, mask));
                CHECK(fixture.interpreter->descriptor() == descriptor);
                CHECK(budget.used() == held);
            }
            CHECK(budget.used() == 0);
        }
    }
    core::Budget budget{nlm_source_budget};
    {
        std::stop_source stop;
        NlmSourceFixture fixture{budget, image::SampleModel::rgb,
                                 core::Cancellation{stop.get_token()}};
        const auto held = budget.used();
        const auto before = gamma_bits(fixture.luminance.view().storage());
        static_cast<void>(request_stop(stop));
        const auto read = extract_nlm_source(fixture);
        REQUIRE(!read);
        CHECK(read.error().code == core::ErrorCode::cancelled);
        CHECK(gamma_bits(fixture.luminance.view().storage()) == before);
        CHECK(budget.used() == held);
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("Producer RGB admission rejects every nonunit channel before eligible or protected use") {
    for (const auto value : {
             -0.01,
             1.01,
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        for (const auto channel : {0U, 1U, 2U}) {
            std::array<double, 3> delivered{0.5, 0.5, 0.5};
            REQUIRE(image::valid_unit_samples(delivered));
            delivered.at(channel) = value;
            const auto before = gamma_bits(delivered);
            CHECK(!image::valid_unit_samples(delivered));
            CHECK(gamma_bits(delivered) == before);
        }
    }
}
} // namespace docenhance::tests
