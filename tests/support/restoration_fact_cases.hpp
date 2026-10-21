// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "require.hpp"
#include "restoration_fixture.hpp"

#include <cmath>
#include <limits>
#include <optional>

namespace docenhance::tests {
inline void restoration_completion_fact_cases() {
    auto facts = completed_wiener_facts();
    const methods::WienerParameters parameters;
    require(methods::validate(facts, parameters, restoration_fact_extent),
            "R01 completed facts retain resolved PSF transform and excursion observations");
    require(!methods::validate(facts, parameters, {.width = 8, .height = 16}),
            "R01 transform dimensions belong to the actual canvas axes");
    facts.raw_low_samples = 129;
    require(!methods::validate(facts),
            "R01 raw excursions cannot exceed evaluated eligible samples");
    facts.raw_low_samples = 5;
    facts.blended_low_samples = 6;
    require(!methods::validate(facts), "R01 blended low excursions require raw low candidates");
    facts.blended_low_samples = 5;
    facts.blended_high_samples = 7;
    facts.corrected_samples = 80;
    facts.counts.changed = 67;
    const methods::WienerParameters full{.blend = 1};
    require(!methods::validate(facts, full, restoration_fact_extent),
            "full R01 blend writes every corrected candidate that is not clipped");
    facts.counts.changed = 68;
    require(methods::validate(facts, full, restoration_fact_extent),
            "full R01 blend admits its exact unclipped correction lower bound");
    facts.blended_high_samples = 6;
    facts.counts.changed = 69;
    require(methods::validate(facts),
            "full-blend classification control passes independent generic bounds");
    require(methods::validate(facts, parameters, restoration_fact_extent),
            "partial-blend classification control is a healthy contextual baseline");
    require(!methods::validate(facts, full, restoration_fact_extent),
            "full R01 blend preserves raw excursion classification");
}
inline void restoration_bypass_fact_cases() {
    methods::WienerFacts facts{
        .reason = methods::WienerReason::zero_blend,
        .counts = {.eligible = 128},
        .psf = analytic_psf_facts(),
    };
    const methods::WienerParameters zero{.blend = 0};
    require(methods::validate(facts, zero, restoration_fact_extent),
            "R01 zero blend retains validated PSF facts without transform work");
    facts.counts = {.protected_samples = 128};
    require(methods::validate(facts, zero, restoration_fact_extent),
            "R01 zero blend precedes fully protected completion");
    facts.reason = methods::WienerReason::no_eligible_samples;
    const methods::WienerParameters parameters;
    require(methods::validate(facts, parameters, restoration_fact_extent),
            "fully protected R01 completion performs no transform");
    facts.psf.normalized_sum = 0;
    require(!methods::validate(facts), "R01 validates its PSF even for no-op completion");
}
inline void restoration_psf_fact_cases() {
    auto facts = completed_wiener_facts();
    facts.psf.kind = methods::PsfKind::kernel;
    const methods::WienerParameters parameters;
    require(!methods::validate(facts, parameters, restoration_fact_extent),
            "R01 resolved PSF kind agrees with the selected request");
    const methods::WienerParameters kernel{.psf = methods::Kernel{}};
    require(methods::validate(facts, kernel, restoration_fact_extent),
            "R01 kernel scalar admission borrows its path without storing it in facts");
    facts.psf.centroid_x = 1e300;
    require(!methods::validate(facts), "R01 finite PSF centroids have bounded spatial support");
    facts.psf = analytic_psf_facts(methods::MotionPsf{});
    const methods::WienerParameters motion{.psf = methods::MotionPsf{}};
    require(methods::validate(facts, motion, restoration_fact_extent),
            "R01 motion facts use the actual analytic PSF owner");
    facts.psf.width = 4;
    require(!methods::validate(facts), "R01 PSF support remains odd and centered");
}
inline void restoration_no_effect_fact_cases() {
    auto facts = completed_wiener_facts();
    facts.disposition = methods::Disposition::no_change;
    facts.reason = methods::WienerReason::no_effect;
    facts.counts.changed = 0;
    facts.corrected_samples = 0;
    facts.raw_low_samples = 0;
    facts.raw_high_samples = 0;
    facts.blended_low_samples = 0;
    facts.blended_high_samples = 0;
    const methods::WienerParameters parameters;
    require(methods::validate(facts, parameters, restoration_fact_extent),
            "active R01 no-effect completion retains its actual transform and mean");
    facts.transform = std::nullopt;
    require(!methods::validate(facts), "R01 no-effect is not an unprepared bypass");
}
inline void restoration_blended_count_cases() {
    auto facts = tiny_blend_wiener_facts();
    const methods::WienerParameters tiny{.blend = std::numeric_limits<double>::denorm_min()};
    require(facts.raw_low_samples == 1 && facts.corrected_samples == 0 && facts.counts.changed == 0,
            "actual R01 scalar blending can round a raw excursion back to entering unit luminance");
    require(methods::validate(facts, tiny, restoration_fact_extent),
            "R01 raw excursions need not be preclamp blended corrections");
    facts = completed_wiener_facts();
    facts.raw_low_samples = 64;
    facts.raw_high_samples = 65;
    require(!methods::validate(facts), "R01 disjoint raw excursions cannot exceed evaluation");
    facts = completed_wiener_facts();
    facts.corrected_samples = 4;
    facts.counts.changed = 0;
    require(!methods::validate(facts), "R01 disjoint blended excursions cannot exceed corrections");
    facts = completed_wiener_facts();
    facts.counts.changed = 59;
    require(!methods::validate(facts),
            "every unexcursed blended correction causes a D3 write at every blend");
}
inline void restoration_centroid_cases() {
    auto facts = edge_psf_facts(3, 3, true, true);
    require(methods::validate(facts),
            "actual normalized small asymmetric edge PSF remains admitted");
    facts.centroid_x = 1.5;
    require(!methods::validate(facts),
            "a three-by-three PSF cannot have a one-and-half pixel centroid");
    facts.centroid_x = -1.5;
    require(!methods::validate(facts), "PSF support bounds cover negative centroid displacement");
    for (const auto positive : {false, true}) {
        const auto vertical = edge_psf_facts(3, 5, true, positive);
        require(methods::validate(vertical),
                "small odd rectangular PSF edge support remains valid");
        require(positive ? vertical.centroid_x > 0 : vertical.centroid_x < 0,
                "the actual PSF moment preserves horizontal sign");
        const auto horizontal = edge_psf_facts(129, 129, false, positive);
        require(methods::validate(horizontal),
                "maximum-side asymmetric normalized PSF remains valid");
        require(horizontal.normalized_sum != 1,
                "actual ordered normalized sum rounding must not be replaced by a literal one");
        require(positive ? horizontal.centroid_y > 0 : horizontal.centroid_y < 0,
                "the actual PSF moment preserves vertical sign");
    }
    facts = edge_psf_facts(129, 129, false, true);
    constexpr double roundoff_factor = 4;
    const auto factor = 1 + (roundoff_factor * static_cast<double>(facts.width * facts.height) *
                             std::numeric_limits<double>::epsilon());
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto inner = std::nextafter(facts.normalized_sum * factor, infinity);
    const auto bound =
        std::nextafter(((static_cast<double>(facts.height) - 1) / 2) * inner, infinity);
    facts.centroid_y = std::nextafter(bound, infinity);
    require(!methods::validate(facts),
            "one ULP outside the two-product centroid envelope is refused");
}
inline void restoration_fact_cases() {
    restoration_completion_fact_cases();
    restoration_bypass_fact_cases();
    restoration_psf_fact_cases();
    restoration_no_effect_fact_cases();
    restoration_blended_count_cases();
    restoration_centroid_cases();
}
} // namespace docenhance::tests
