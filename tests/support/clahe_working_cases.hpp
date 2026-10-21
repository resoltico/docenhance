// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "clahe_reference.hpp"
#include "docenhance/methods/clahe.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace docenhance::tests {
inline constexpr methods::ClaheParameters small_clahe_parameters{.grid_columns = 2, .grid_rows = 2};
inline methods::ClaheFacts check_clahe_reference(ClaheFixture& fixture,
                                                 methods::ClaheParameters parameters,
                                                 unsigned workers) {
    const auto expected = clahe_reference(fixture, parameters);
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> original_mask{mask.begin(), mask.end()};
    const auto charge = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
    const auto result =
        methods::apply(methods::Clahe::create(parameters).value(),
                       {fixture.luminance.view(), fixture.protection.view().as_const(),
                        fixture.budget, scheduler});
    require(result.has_value(), "actual Working CLAHE succeeds");
    require(*result == expected.facts && methods::validate(*result, parameters),
            "CLAHE counts and closed facts match independent fitting and D3 mapping");
    const auto view = fixture.luminance.view();
    for (std::size_t i = 0; i < expected.samples.size(); ++i) {
        const auto actual = view.storage().subspan(i, 1).front();
        if (std::bit_cast<std::uint64_t>(expected.samples.at(i)) == before.at(i)) {
            require(std::bit_cast<std::uint64_t>(actual) == before.at(i),
                    "CLAHE exact identity/protected/padding backing bits survive");
        } else {
            require(std::isfinite(actual) && actual >= 0 && actual <= 1 &&
                        std::abs(actual - expected.samples.at(i)) < 1e-12,
                    "actual CLAHE output matches independent finite-unit scalar reference");
        }
    }
    require(std::ranges::equal(mask, original_mask), "CLAHE never writes mask storage");
    require(fixture.budget.used() == charge, "CLAHE refunds all method maps after joined return");
    return *result;
}
inline void clahe_protect_fixture(ClaheFixture& fixture) {
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        for (std::uint32_t x = 0; x < fixture.luminance.width(); ++x) {
            if ((x + (3 * y)) % 7 == 0 || (y >= 10 && y < 16)) {
                fixture.protection.view().row(y).subspan(x, 1).front() = 255;
                fixture.luminance.view().row(y).subspan(x, 1).front() = -0.0;
            }
        }
    }
}
inline void clahe_workers_case(std::uint32_t width, std::uint32_t height,
                               methods::ClaheParameters p) {
    std::vector<std::uint64_t> baseline;
    methods::ClaheFacts original;
    for (const unsigned workers : {1U, 2U, 7U}) {
        ClaheFixture fixture{width, height, clahe_scratch_bytes(p.grid_columns * p.grid_rows)};
        clahe_protect_fixture(fixture);
        const auto facts = check_clahe_reference(fixture, p, workers);
        require(facts.counts.changed != 0, "CLAHE reference exercises actual active writes");
        const auto bits = gamma_bits(fixture.luminance.view().storage());
        if (baseline.empty()) {
            baseline = bits;
            original = facts;
        } else {
            require(bits == baseline && facts == original,
                    "CLAHE output backing and facts are bit-identical for1/2/7 workers");
        }
    }
}
inline void clahe_active_cases() {
    clahe_workers_case(33, 35, small_clahe_parameters);
    clahe_workers_case(65, 67, {.grid_columns = 3, .grid_rows = 4, .clip = 8, .blend = 0.6});
    clahe_workers_case(67, 51, {.grid_columns = 4, .grid_rows = 3, .clip = 1});
}
inline void clahe_identity_cases() {
    ClaheFixture constant{33, 35, clahe_scratch_bytes(4)};
    for (std::uint32_t y = 0; y < constant.luminance.height(); ++y) {
        std::ranges::fill(constant.luminance.view().row(y), 0.4);
    }
    const auto all = check_clahe_reference(constant, small_clahe_parameters, 7);
    require(all.identity_tiles == 4 && all.counts.evaluated == 1155 && all.counts.changed == 0,
            "all identity maps still complete active evaluation without writes");
    ClaheFixture mixed{33, 35, clahe_scratch_bytes(4)};
    for (std::uint32_t y = 0; y < 17; ++y) {
        std::ranges::fill(mixed.luminance.view().row(y).first(16), 0.4);
    }
    const auto before = gamma_bits(mixed.luminance.view().storage());
    const auto facts = check_clahe_reference(mixed, small_clahe_parameters, 2);
    require(facts.identity_tiles == 1, "mixed fitting retains the actual constant tile flag");
    bool changed_inside = false;
    for (std::uint32_t y = 0; y < 17; ++y) {
        for (std::uint32_t x = 0; x < 16; ++x) {
            const auto i = (std::size_t{y} * mixed.luminance.view().row_pitch()) + x;
            changed_inside =
                changed_inside ||
                std::bit_cast<std::uint64_t>(mixed.luminance.view().storage()[i]) != before.at(i);
        }
    }
    require(changed_inside, "neighboring nonidentity maps affect destinations in an identity tile");
    ClaheFixture tiny{33, 35, clahe_scratch_bytes(4)};
    const auto rounded = check_clahe_reference(
        tiny,
        {.grid_columns = 2, .grid_rows = 2, .blend = std::numeric_limits<double>::denorm_min()}, 1);
    require(rounded.counts.changed == 0 && rounded.counts.evaluated == 1155 &&
                rounded.identity_tiles != 4,
            "nonidentity maps do not imply writes under a rounded tiny blend");
}
inline void clahe_cutoff_case(std::uint32_t count, double range) {
    ClaheFixture fixture{32, 32, clahe_scratch_bytes(4)};
    std::ranges::fill(fixture.protection.view().storage(), 255);
    for (std::uint32_t x = 0; x < count; ++x) {
        fixture.protection.view().row(0).subspan(x, 1).front() = 0;
        fixture.luminance.view().row(0).subspan(x, 1).front() = 0;
    }
    fixture.luminance.view().row(0).front() = sharpen_reference_srgb_decode(range);
    const auto attained = sharpen_reference_srgb_encode(fixture.luminance.view().row(0).front());
    require(attained == range, "the actual attained encoded cutoff is exact in this fixture");
    const auto facts = check_clahe_reference(fixture, small_clahe_parameters, 7);
    require(facts.identity_tiles == (count < 16 || attained < 1.0 / 4096 ? 4U : 3U),
            "actual shared fit uses strict count16 and true range1/4096 cutoffs");
}
inline std::uint32_t clahe_dense_bin(std::uint32_t index) {
    if (index == 4095) {
        return 1023;
    }
    return index < 4092 ? index / 4 : 0;
}
inline void clahe_dense_white_fixture(ClaheFixture& fixture) {
    std::array<std::uint32_t, 1024> counts{};
    for (std::uint32_t y = 0; y < 128; ++y) {
        for (std::uint32_t x = 0; x < 128; ++x) {
            const auto i = ((y % 64) * 64) + (x % 64);
            const auto bin = clahe_dense_bin(i);
            const auto value =
                i == 4095 ? 1.0
                          : sharpen_reference_srgb_decode((static_cast<double>(bin) + 0.5) / 1024);
            fixture.luminance.view().row(y).subspan(x, 1).front() = value;
            const auto f = sharpen_reference_srgb_encode(value);
            require(std::min(1023U, static_cast<std::uint32_t>(f * 1024)) == bin,
                    "dense white fixture attains its intended actual histogram bin");
            if (x < 64 && y < 64) {
                ++counts.at(bin);
            }
        }
    }
    require(counts.front() == 7 && counts.back() == 1 &&
                std::ranges::all_of(std::span{counts}.subspan(1, 1022),
                                    [](auto count) { return count == 4; }),
            "each dense tile has4096 samples and max mass7 below clipping limit8");
}
inline void clahe_white_write_case() {
    ClaheFixture fixture{128, 128, clahe_scratch_bytes(4)};
    clahe_dense_white_fixture(fixture);
    const auto maps = clahe_reference_maps(fixture, small_clahe_parameters);
    const auto f = sharpen_reference_srgb_encode(1);
    require(f < 1 && maps.front().knots.at(1023) == 4095.0 / 4096 &&
                clahe_reference_context(f, maps.front()) == 1,
            "a genuine fitted dense map rounds the white candidate to1");
    const auto facts = check_clahe_reference(fixture, small_clahe_parameters, 7);
    require(facts.identity_tiles == 0 && facts.counts.changed >= 4 &&
                fixture.luminance.view().row(63).subspan(63, 1).front() == 1 &&
                fixture.luminance.view().row(127).subspan(127, 1).front() == 1,
            "white one-over-one stores count among all actual independent D3 writes");
}
} // namespace docenhance::tests
