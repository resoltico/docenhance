// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "nlm_working_cases.hpp"

namespace docenhance::tests {
struct NlmBacking {
    explicit NlmBacking(NlmWorkingFixture& fixture)
        : y(gamma_bits(fixture.luminance.view().storage())),
          mask(fixture.protection.view().storage().begin(),
               fixture.protection.view().storage().end()),
          resident(fixture.budget.used()) {}
    std::vector<std::uint64_t> y;
    std::vector<std::uint8_t> mask;
    std::size_t resident;
};
inline void nlm_resource_unchanged(NlmWorkingFixture& fixture, const NlmBacking& before) {
    require(gamma_bits(fixture.luminance.view().storage()) == before.y &&
                std::ranges::equal(fixture.protection.view().storage(), before.mask),
            "resource refusal preserves every entering Y, protection and padding byte");
}
inline void nlm_resource_stores(NlmWorkingFixture& fixture, const NlmBacking& before,
                                const NlmReference& complete) {
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto actual = output[i];
        require(std::bit_cast<std::uint64_t>(actual) == before.y.at(i) ||
                    (std::bit_cast<std::uint64_t>(complete.samples.at(i)) != before.y.at(i) &&
                     std::isfinite(actual) && std::abs(actual - complete.samples.at(i)) < 1e-12),
                "failed joined NLM permits only entering or independently completed stores");
    }
    require(std::ranges::equal(fixture.protection.view().storage(), before.mask) &&
                fixture.budget.used() == before.resident,
            "joined NLM preserves mask backing and refunds to the actual resident baseline");
}
inline void nlm_resource_complete(NlmWorkingFixture& fixture, const NlmBacking& before,
                                  const NlmReference& complete) {
    nlm_resource_stores(fixture, before, complete);
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < output.size(); ++i) {
        require(std::bit_cast<std::uint64_t>(complete.samples.at(i)) == before.y.at(i)
                    ? std::bit_cast<std::uint64_t>(output[i]) == before.y.at(i)
                    : std::abs(output[i] - complete.samples.at(i)) < 1e-12,
                "healthy complete output reaches each independently expected store");
    }
}
inline void nlm_resource_healthy() {
    NlmWorkingFixture fixture{7, 7, nlm_working_test_scratch};
    fixture.protection.view().row(0)[0] = 255;
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto facts = check_nlm_reference(fixture, {.h = 1, .patch = 3, .search = 7}, 1);
    require(facts.counts.evaluated == 48 && facts.counts.protected_samples == 1 &&
                facts.counts.changed > 0 && facts.corrected_samples > 0 &&
                gamma_bits(fixture.luminance.view().storage()) != before,
            "healthy seven-square actual NLM attains nonidentity and checked full reference Facts");
}
} // namespace docenhance::tests
