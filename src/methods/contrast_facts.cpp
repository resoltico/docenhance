// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/contrast_facts.hpp"

#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"

#include <cstddef>
#include <cstdint>

namespace docenhance::methods {
namespace {
bool bypass(Disposition disposition, const PixelCounts& counts) noexcept {
    return validate(counts) && disposition == Disposition::no_change && counts.evaluated == 0 &&
           counts.changed == 0;
}
bool effect(Disposition disposition, const PixelCounts& counts, bool applied) noexcept {
    return validate(counts) && counts.eligible != 0 && counts.evaluated == counts.eligible &&
           (applied ? disposition == Disposition::applied && counts.changed != 0
                    : disposition == Disposition::no_change && counts.changed == 0);
}
} // namespace
bool validate(const GammaFacts& facts) noexcept {
    switch (facts.reason) {
    case GammaReason::none:
        return effect(facts.disposition, facts.counts, true);
    case GammaReason::no_effect:
        return effect(facts.disposition, facts.counts, false);
    case GammaReason::no_eligible_samples:
        return bypass(facts.disposition, facts.counts) && facts.counts.eligible == 0;
    case GammaReason::identity_gamma:
        return bypass(facts.disposition, facts.counts) && facts.counts.eligible != 0;
    case GammaReason::zero_blend:
        return bypass(facts.disposition, facts.counts);
    }
    return false;
}
bool validate(const ClaheFacts& facts) noexcept {
    if (facts.identity_tiles > clahe_tiles_max) {
        return false;
    }
    switch (facts.reason) {
    case ClaheReason::none:
        return effect(facts.disposition, facts.counts, true);
    case ClaheReason::no_effect:
        return effect(facts.disposition, facts.counts, false);
    case ClaheReason::no_eligible_samples:
        return bypass(facts.disposition, facts.counts) && facts.counts.eligible == 0 &&
               facts.identity_tiles == 0;
    case ClaheReason::zero_blend:
        return bypass(facts.disposition, facts.counts) && facts.identity_tiles == 0;
    }
    return false;
}
bool validate(const LevelsFacts& facts) noexcept {
    const auto& counts = facts.counts;
    if (!validate(counts) || facts.clipped_low_samples > counts.evaluated ||
        facts.clipped_high_samples > counts.evaluated - facts.clipped_low_samples ||
        (facts.levels && !validate(*facts.levels))) {
        return false;
    }
    if (facts.levels &&
        ((facts.levels->low == 0 && facts.clipped_low_samples != 0) ||
         (facts.levels->high == 1 && facts.clipped_high_samples != 0) ||
         (facts.levels->low == 0 && facts.levels->high == 1 && counts.changed != 0))) {
        return false;
    }
    switch (facts.reason) {
    case LevelsReason::zero_blend:
        return bypass(facts.disposition, counts) && !facts.levels;
    case LevelsReason::no_eligible_samples:
        return bypass(facts.disposition, counts) && counts.eligible == 0 && !facts.levels;
    case LevelsReason::insufficient_dynamic_range:
        return bypass(facts.disposition, counts) && counts.eligible != 0 && facts.levels &&
               facts.levels->high - facts.levels->low < levels_minimum_range;
    case LevelsReason::none:
        return effect(facts.disposition, counts, true) && facts.levels &&
               facts.levels->high - facts.levels->low >= levels_minimum_range;
    case LevelsReason::no_effect:
        return effect(facts.disposition, counts, false) && facts.levels &&
               facts.levels->high - facts.levels->low >= levels_minimum_range;
    }
    return false;
}
bool validate(const GammaFacts& facts, GammaParameters parameters) noexcept {
    if (!validate(facts) || !valid_parameters(parameters)) {
        return false;
    }
    if (parameters.blend == 0) {
        return facts.reason == GammaReason::zero_blend;
    }
    if (facts.counts.eligible == 0) {
        return facts.reason == GammaReason::no_eligible_samples;
    }
    if (parameters.gamma == 1) {
        return facts.reason == GammaReason::identity_gamma;
    }
    return facts.reason == GammaReason::none || facts.reason == GammaReason::no_effect;
}
bool validate(const LevelsFacts& facts, LevelsParameters parameters) noexcept {
    if (!validate(facts) || !valid_parameters(parameters) || parameters.high <= parameters.low) {
        return false;
    }
    if (parameters.blend == 0) {
        return facts.reason == LevelsReason::zero_blend;
    }
    if (facts.counts.eligible == 0) {
        return facts.reason == LevelsReason::no_eligible_samples;
    }
    if (!facts.levels) {
        return false;
    }
    const auto& bounds = *facts.levels;
    if (parameters.blend == 1 && bounds.high - bounds.low >= levels_minimum_range) {
        const auto endpoint_writes = std::uint64_t{bounds.low > 0} + std::uint64_t{bounds.high < 1};
        if (facts.counts.changed < endpoint_writes) {
            return false;
        }
    }
    const auto count = static_cast<std::size_t>(facts.counts.eligible);
    constexpr double percent_scale = 100;
    const auto low = image::nearest_rank_index(count, parameters.low / percent_scale);
    const auto high = image::nearest_rank_index(count, parameters.high / percent_scale);
    return low && high && facts.clipped_low_samples <= *low &&
           facts.clipped_high_samples <= count - 1 - *high &&
           (*low != *high || bounds.low == bounds.high);
}
bool validate(const ClaheFacts& facts, ClaheParameters parameters) noexcept {
    if (!validate(facts) || !valid_parameters(parameters) ||
        facts.identity_tiles > parameters.grid_columns * parameters.grid_rows) {
        return false;
    }
    if (parameters.blend == 0) {
        return facts.reason == ClaheReason::zero_blend;
    }
    if (facts.counts.eligible == 0) {
        return facts.reason == ClaheReason::no_eligible_samples;
    }
    const auto tiles = std::uint64_t{parameters.grid_columns} * parameters.grid_rows;
    if (facts.counts.eligible + facts.counts.protected_samples <
            tiles * clahe_minimum_tile * clahe_minimum_tile ||
        (facts.identity_tiles == tiles && facts.counts.changed != 0) ||
        tiles - facts.identity_tiles > facts.counts.eligible / clahe_minimum_samples) {
        return false;
    }
    return facts.reason == ClaheReason::none || facts.reason == ClaheReason::no_effect;
}
} // namespace docenhance::methods
