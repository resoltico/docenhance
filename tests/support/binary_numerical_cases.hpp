// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "binary_fixture.hpp"
#include "binary_reference.hpp"
#include "binary_wide_cases.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace docenhance::tests {
inline void binary_fixed_minimal_case() {
    constexpr std::uint16_t middle = 32768;
    std::array<std::uint16_t, 7> codes{0, 1, 32767, binary_padding, middle, 65534, 65535};
    const auto before = codes;
    std::array<std::uint8_t, 9> destination{};
    const auto input = image::PlaneView<const std::uint16_t>::create(
                           codes, {.width = 3, .height = 2, .stride = 4 * sizeof(std::uint16_t)})
                           .value();
    const auto output =
        image::PlaneView<std::uint8_t>::create(destination, {.width = 3, .height = 2, .stride = 5})
            .value();
    core::Budget budget{binary_test_budget};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(7, 7, 0, 0).value()};
    const auto at = static_cast<double>(middle) / binary_reference_max;
    for (const auto threshold :
         {0.0, 0.5, 1.0, at, std::nextafter(at, 0.0), std::nextafter(at, 1.0)}) {
        destination.fill(binary_sentinel);
        const auto method = methods::FixedThreshold::create(threshold).value();
        const auto result = methods::binarize(method, {input, output, budget, scheduler});
        require(result && result->black_samples + result->white_samples == 6 &&
                    budget.used() == 0 && codes == before,
                "actual minimal-row Q16 fixed returns complete counts/refund/source");
        std::uint64_t black = 0;
        for (std::uint32_t y = 0; y < input.height(); ++y) {
            for (std::uint32_t x = 0; x < input.width(); ++x) {
                const bool expected = static_cast<double>(input.row(y).subspan(x, 1).front()) /
                                          binary_reference_max <=
                                      threshold;
                black += static_cast<std::uint64_t>(expected);
                require(output.row(y).subspan(x, 1).front() == (expected ? 0 : 1),
                        "fixed literal Q16 equality/endpoints/adjacent thresholds");
            }
        }
        require(result->black_samples == black && destination.at(3) == binary_sentinel &&
                    destination.at(4) == binary_sentinel && destination.at(8) == binary_sentinel,
                "actual fixed counts and complete noncanonical output padding agree");
    }
}
inline void binary_old_embedding_case() {
    BinaryFixture fixture{256, 1};
    for (std::uint32_t i = 0; i < 256; ++i) {
        fixture.codes.view().row(0).subspan(i, 1).front() = static_cast<std::uint16_t>(257 * i);
    }
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    for (const auto threshold : {0.0, 0.5, 1.0}) {
        const auto result = methods::binarize(
            methods::FixedThreshold::create(threshold).value(),
            {fixture.codes.view().as_const(), fixture.out.view(), fixture.budget, scheduler});
        require(result.has_value(), "actual all256 old-domain embedding completes");
        for (std::uint32_t i = 0; i < 256; ++i) {
            const bool black = static_cast<double>(i) / 255.0 <= threshold;
            require(fixture.out.view().row(0).subspan(i, 1).front() == (black ? 0 : 1),
                    "all256 Q257*s fixed decisions preserve old classification, direct0/1");
        }
    }
}
inline void binary_otsu_case(BinaryFixture& fixture) {
    const auto codes = fixture.codes.view().as_const();
    const auto reference = binary_otsu_reference(codes);
    const auto source = codes.storage();
    const std::vector<std::uint16_t> before(source.begin(), source.end());
    const auto held = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = methods::binarize(methods::Otsu::create(),
                                          {codes, fixture.out.view(), fixture.budget, scheduler});
    require(result && result->threshold_bin == reference.threshold &&
                result->single_bin_fallback == reference.fallback &&
                fixture.budget.used() == held && std::ranges::equal(source, before),
            "actual Q16 Otsu matches independent bin-merged fit");
    std::uint64_t black = 0;
    for (std::uint32_t y = 0; y < codes.height(); ++y) {
        for (std::uint32_t x = 0; x < codes.width(); ++x) {
            const bool expected =
                binary_reference_bin(codes.row(y).subspan(x, 1).front()) <= reference.threshold;
            black += static_cast<std::uint64_t>(expected);
            require(fixture.out.view().row(y).subspan(x, 1).front() == (expected ? 0 : 1),
                    "actual Q16 Otsu classification uses the selected immutable fit");
        }
    }
    require(result->black_samples == black &&
                result->white_samples == (std::uint64_t{codes.width()} * codes.height()) - black,
            "actual Otsu complete counters match independent classification");
}
inline void binary_otsu_collision_cases() {
    for (const auto low : {std::uint16_t{0}, std::uint16_t{65534}}) {
        BinaryFixture fixture{2, 1};
        fixture.codes.view().row(0).front() = low;
        fixture.codes.view().row(0).back() = static_cast<std::uint16_t>(low + 1);
        binary_otsu_case(fixture);
    }
    for (const auto count : {1000U, 10000U}) {
        BinaryFixture fixture{(2 * count) + 2, 1};
        const auto row = fixture.codes.view().row(0);
        std::ranges::fill(row.first(count), std::uint16_t{0});
        row.subspan(count, 1).front() = 21845;
        std::ranges::fill(row.subspan(count + 1), std::uint16_t{43690});
        binary_otsu_case(fixture);
        require(fixture.out.view().row(0).subspan(count, 1).front() == (count == 10000 ? 1 : 0),
                "actual global maximum then earliest relative tolerance distinguishes near ties");
    }
}
inline void binary_sauvola_small_case(std::uint32_t width, std::uint32_t height) {
    BinaryFixture fixture{width, height};
    const auto source = fixture.codes.view().storage();
    const std::vector<std::uint16_t> before(source.begin(), source.end());
    const auto held = fixture.budget.used();
    const auto method = methods::Sauvola::create({.window = 7}).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(2, 2, 0, 0).value()};
    const auto result = methods::binarize(
        method, {fixture.codes.view().as_const(), fixture.out.view(), fixture.budget, scheduler});
    require(result && fixture.budget.used() == held && std::ranges::equal(source, before),
            "actual small Q16 Sauvola completes and refunds immutable source");
    std::uint64_t black = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto expected =
                binary_sauvola_reference(fixture.codes.view().as_const(), method, x, y);
            require(fixture.out.view().row(y).subspan(x, 1).front() == expected,
                    "actual Q16 strips match independent direct reflected integer windows");
            black += static_cast<std::uint64_t>(expected == 0);
        }
    }
    require(result->black_samples == black &&
                result->white_samples == (std::uint64_t{width} * height) - black,
            "actual Sauvola counts match direct-window classifications");
}
inline void binary_numerical_cases() {
    binary_wide_cases();
    binary_fixed_minimal_case();
    binary_old_embedding_case();
    binary_otsu_collision_cases();
    binary_sauvola_small_case(1, 3);
    binary_sauvola_small_case(13, 1);
    binary_sauvola_small_case(19, 11);
}
} // namespace docenhance::tests
