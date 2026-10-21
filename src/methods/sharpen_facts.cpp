// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/sharpen_facts.hpp"

#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/sharpening.hpp"

namespace docenhance::methods {
namespace {
bool empty_application(const UnsharpFacts& facts) noexcept {
    return facts.counts.evaluated == 0 && facts.counts.changed == 0 && facts.context_samples == 0 &&
           facts.corrected_samples == 0 && !facts.pre_clamp && facts.clipped_low_samples == 0 &&
           facts.clipped_high_samples == 0;
}
bool active_application(const UnsharpFacts& facts) noexcept {
    if (!facts.pre_clamp || !validate(*facts.pre_clamp) || facts.counts.eligible == 0 ||
        facts.counts.evaluated != facts.counts.eligible ||
        facts.context_samples != facts.counts.eligible + facts.counts.protected_samples) {
        return false;
    }
    if ((facts.counts.evaluated == 1 && facts.pre_clamp->low != facts.pre_clamp->high) ||
        (facts.context_samples == 1 && facts.corrected_samples != 0)) {
        return false;
    }
    return (facts.pre_clamp->low < 0) == (facts.clipped_low_samples != 0) &&
           (facts.pre_clamp->high > 1) == (facts.clipped_high_samples != 0) &&
           (facts.pre_clamp->high < 0) == (facts.clipped_low_samples == facts.counts.evaluated) &&
           (facts.pre_clamp->low > 1) == (facts.clipped_high_samples == facts.counts.evaluated);
}
} // namespace
bool validate(const UnsharpFacts& facts) noexcept {
    const auto& counts = facts.counts;
    if (!validate(counts) || facts.corrected_samples > counts.evaluated ||
        counts.changed > facts.corrected_samples ||
        facts.clipped_low_samples > facts.corrected_samples ||
        facts.clipped_high_samples > facts.corrected_samples - facts.clipped_low_samples) {
        return false;
    }
    if (counts.changed <
        facts.corrected_samples - facts.clipped_low_samples - facts.clipped_high_samples) {
        return false;
    }
    switch (facts.reason) {
    case UnsharpReason::zero_amount:
        return facts.disposition == Disposition::no_change && empty_application(facts);
    case UnsharpReason::no_eligible_samples:
        return facts.disposition == Disposition::no_change && counts.eligible == 0 &&
               empty_application(facts);
    case UnsharpReason::none:
        return facts.disposition == Disposition::applied && counts.changed != 0 &&
               active_application(facts);
    case UnsharpReason::no_effect:
        return facts.disposition == Disposition::no_change && counts.changed == 0 &&
               active_application(facts);
    }
    return false;
}
bool validate(const UnsharpFacts& facts, UnsharpParameters parameters) noexcept {
    if (!validate(facts) || !valid_parameters(parameters)) {
        return false;
    }
    if (parameters.amount == 0) {
        return facts.reason == UnsharpReason::zero_amount;
    }
    if (facts.counts.eligible == 0) {
        return facts.reason == UnsharpReason::no_eligible_samples;
    }
    if (!facts.pre_clamp) {
        return false;
    }
    const auto method = Unsharp::create(parameters);
    if (!method) {
        return false;
    }
    const auto low = unsharp_candidate(0, 1, *method);
    const auto high = unsharp_candidate(1, 0, *method);
    return facts.reason != UnsharpReason::zero_amount &&
           facts.reason != UnsharpReason::no_eligible_samples && facts.pre_clamp->low >= low &&
           facts.pre_clamp->high <= high;
}
} // namespace docenhance::methods
