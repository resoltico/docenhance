// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/nlm_facts.hpp"

#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"

#include <cmath>
#include <cstdint>

namespace docenhance::methods {
std::uint32_t nlm_weight_entries(const Nlm& method) noexcept {
    const auto parameters = method.parameters();
    const double eta = parameters.h / image::byte_max;
    const double patch_samples = parameters.patch * parameters.patch;
    constexpr double cutoff_sigmas = 3;
    return static_cast<std::uint32_t>(
        std::ceil(cutoff_sigmas * eta * patch_samples * image::word_max));
}
bool validate(const NlmFacts& facts) noexcept {
    const auto& counts = facts.counts;
    if (!validate(counts) || facts.corrected_samples > counts.evaluated ||
        counts.changed > facts.corrected_samples) {
        return false;
    }
    switch (facts.reason) {
    case NlmReason::zero_blend:
        return facts.disposition == Disposition::no_change && counts.evaluated == 0 &&
               counts.changed == 0 && facts.corrected_samples == 0 &&
               facts.weight_table_entries == 0;
    case NlmReason::no_eligible_samples:
        return facts.disposition == Disposition::no_change && counts.eligible == 0 &&
               counts.evaluated == 0 && counts.changed == 0 && facts.corrected_samples == 0 &&
               facts.weight_table_entries == 0;
    case NlmReason::none:
        return facts.disposition == Disposition::applied && counts.changed != 0 &&
               counts.evaluated == counts.eligible && facts.weight_table_entries != 0;
    case NlmReason::no_effect:
        return facts.disposition == Disposition::no_change && counts.changed == 0 &&
               counts.eligible != 0 && counts.evaluated == counts.eligible &&
               facts.weight_table_entries != 0;
    }
    return false;
}
bool validate(const NlmFacts& facts, NlmParameters parameters) noexcept {
    if (!validate(facts) || !valid_parameters(parameters) || parameters.search < parameters.patch) {
        return false;
    }
    if (parameters.blend == 0) {
        return facts.reason == NlmReason::zero_blend;
    }
    if (facts.counts.eligible == 0) {
        return facts.reason == NlmReason::no_eligible_samples;
    }
    const auto method = Nlm::create(parameters);
    if (!method || facts.weight_table_entries != nlm_weight_entries(*method) ||
        facts.counts.eligible + facts.counts.protected_samples <
            std::uint64_t{parameters.search} * parameters.search) {
        return false;
    }
    return facts.reason == NlmReason::none || facts.reason == NlmReason::no_effect;
}
} // namespace docenhance::methods
