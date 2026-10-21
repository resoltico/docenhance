// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "nlm_working_cases.hpp"
#include "nlm_working_fixture.hpp"
#include "nlm_working_reference.hpp"
#include "require.hpp"
#include "sharpen_reference.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
namespace docenhance::tests {
inline void nlm_constant_code_cases() {
    for (const auto white : {false, true}) {
        NlmWorkingFixture fixture{7, 7, nlm_working_test_scratch};
        for (std::uint32_t y = 0; y < 7; ++y) {
            for (std::uint32_t x = 0; x < 7; ++x) {
                const auto fraction = static_cast<double>(1 + ((x + y) % 3)) / 10;
                const auto f = white ? (65535 - fraction) / 65535 : fraction / 65535;
                fixture.luminance.view().row(y).subspan(x, 1).front() =
                    sharpen_reference_srgb_decode(f);
            }
        }
        fixture.luminance.view().row(0).front() = white ? 1.0 : -0.0;
        const auto before = gamma_bits(fixture.luminance.view().storage());
        const auto facts = check_nlm_reference(fixture, {.h = 0.1, .patch = 3, .search = 7}, 1);
        require(facts.weight_table_entries == 694 && facts.counts.evaluated == 49 &&
                    facts.corrected_samples == 0 && facts.counts.changed == 0 &&
                    gamma_bits(fixture.luminance.view().storage()) == before,
                "actual active Q0/Q65535 with differing between-code Y preserves every entering "
                "bit without E/D");
    }
}
inline void nlm_border_and_mean_cases() {
    NlmWorkingFixture border{7, 7, nlm_working_test_scratch};
    constexpr std::array<std::uint16_t, 7> row{0, 100, 1000, 0, 0, 0, 0};
    for (std::uint32_t y = 0; y < 7; ++y) {
        for (std::uint32_t x = 0; x < 7; ++x) {
            border.luminance.view().row(y).subspan(x, 1).front() =
                sharpen_reference_srgb_decode(row.at(x) / 65535.0);
        }
    }
    const methods::NlmParameters p{.h = 3, .patch = 3, .search = 7};
    const auto reference = nlm_working_reference(border, p);
    require(nlm_reference_distance(border, reference, {.x = 0, .y = 3}, {.x = 1, .y = 0}, 3) ==
                3300,
            "independent admitted shifted-border patch has exact3300 rather than "
            "reflected-difference5700");
    check_nlm_reference(border, p, 1);
    NlmWorkingFixture fractional{7, 7, nlm_working_test_scratch};
    const methods::NlmParameters settings{.h = 1, .patch = 3, .search = 7};
    const auto direct = nlm_working_reference(fractional, settings);
    const auto mean = nlm_reference_mean(fractional, direct, 0, 0, settings);
    require(mean != std::floor(mean),
            "actual direct-window reference attains a noninteger weighted mean");
    const auto f = sharpen_reference_srgb_encode(fractional.luminance.view().row(0).front());
    const auto rounded =
        std::clamp(f + ((std::round(mean) - direct.codes.front()) / 65535), 0.0, 1.0);
    const auto alternative =
        sharpen_reference_srgb_decode(((1 - settings.blend) * f) + (settings.blend * rounded));
    check_nlm_reference(fractional, settings, 1);
    require(std::abs(fractional.luminance.view().row(0).front() - alternative) > 1e-10,
            "real Working result distinguishes unrounded Qbar from obsolete rounded uint16 Qd");
}
inline void nlm_decoded_equal_case() {
    NlmWorkingFixture fixture{7, 7, nlm_working_test_scratch};
    for (std::uint32_t y = 0; y < 7; ++y) {
        std::ranges::fill(fixture.luminance.view().row(y), 0.0009);
    }
    fixture.luminance.view().row(0).front() = 0.001;
    const auto entering = fixture.luminance.view().row(0).front();
    const methods::NlmParameters p{.h = 1, .patch = 3, .search = 7, .blend = 5e-16};
    const auto reference = nlm_working_reference(fixture, p);
    const auto mean = nlm_reference_mean(fixture, reference, 0, 0, p);
    const auto f = sharpen_reference_srgb_encode(entering);
    const auto fc = std::clamp(f + ((mean - reference.codes.front()) / 65535), 0.0, 1.0);
    const auto next = ((1 - p.blend) * f) + (p.blend * fc);
    require(mean != reference.codes.front() && next != f &&
                std::bit_cast<std::uint64_t>(reference.samples.front()) ==
                    std::bit_cast<std::uint64_t>(entering),
            "independent real-image mean/fn attains a decoded-equal logical write at the low "
            "linear IEC branch");
    const auto facts = check_nlm_reference(fixture, p, 1);
    require(facts.counts.changed > 0 && facts.corrected_samples > 0 &&
                std::bit_cast<std::uint64_t>(fixture.luminance.view().row(0).front()) ==
                    std::bit_cast<std::uint64_t>(entering),
            "actual Working method counts the fn!=f write although decoded Y preserves exact "
            "entering bits");
}
} // namespace docenhance::tests
