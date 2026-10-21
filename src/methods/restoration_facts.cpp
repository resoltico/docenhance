// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/restoration_facts.hpp"

#include "docenhance/core/range.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration.hpp"
#include "restoration_detail.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace docenhance::methods {
namespace {
double upper_product(double left, double right) noexcept {
    return std::nextafter(left * right, std::numeric_limits<double>::infinity());
}
double centroid_bound(const PsfFacts& facts, std::uint32_t side) noexcept {
    const auto samples = facts.width * facts.height;
    constexpr double roundoff_factor = 4;
    const auto factor = 1 + (roundoff_factor * static_cast<double>(samples) *
                             std::numeric_limits<double>::epsilon());
    const auto radius = (static_cast<double>(side) - 1) / 2;
    return upper_product(radius, upper_product(facts.normalized_sum, factor));
}
} // namespace
bool validate(const PsfFacts& facts) noexcept {
    if (facts.width < psf_minimum_side || facts.height < psf_minimum_side ||
        facts.width > psf_maximum_side || facts.height > psf_maximum_side || facts.width % 2 == 0 ||
        facts.height % 2 == 0 || !std::isfinite(facts.normalized_sum) ||
        std::abs(facts.normalized_sum - 1) > psf_normalization_tolerance ||
        !core::in_range(facts.centroid_x, -centroid_bound(facts, facts.width),
                        centroid_bound(facts, facts.width)) ||
        !core::in_range(facts.centroid_y, -centroid_bound(facts, facts.height),
                        centroid_bound(facts, facts.height))) {
        return false;
    }
    switch (facts.kind) {
    case PsfKind::gaussian:
    case PsfKind::motion:
    case PsfKind::kernel:
        return true;
    }
    return false;
}
namespace {
bool smooth(std::uint32_t value) noexcept {
    constexpr std::array factors{2U, 3U, 5U};
    if (value == 0) {
        return false;
    }
    for (const auto factor : factors) {
        while (value % factor == 0) {
            value /= factor;
        }
    }
    return value == 1;
}
bool untouched(const WienerFacts& facts) noexcept {
    return facts.counts.evaluated == 0 && facts.counts.changed == 0 && facts.context_samples == 0 &&
           facts.corrected_samples == 0 && !facts.transform && facts.raw_low_samples == 0 &&
           facts.raw_high_samples == 0 && facts.blended_low_samples == 0 &&
           facts.blended_high_samples == 0;
}
bool active(const WienerFacts& facts) noexcept {
    return facts.counts.eligible != 0 && facts.counts.evaluated == facts.counts.eligible &&
           facts.context_samples == facts.counts.eligible + facts.counts.protected_samples &&
           facts.transform && validate(*facts.transform) &&
           facts.transform->guard == restoration_guard(facts.psf.width, facts.psf.height);
}
} // namespace
bool validate(const RestorationTransform& transform) noexcept {
    return transform.guard >= restoration_minimum_guard && transform.guard <= fft_max_guard &&
           transform.width <= restoration_fft_maximum &&
           transform.height <= restoration_fft_maximum && transform.width > 2 * transform.guard &&
           transform.height > 2 * transform.guard && smooth(transform.width) &&
           smooth(transform.height) && core::in_range(transform.padded_mean, 0.0, 1.0);
}
bool validate(const WienerFacts& facts) noexcept {
    if (!validate(facts.counts) || !validate(facts.psf) ||
        facts.corrected_samples > facts.counts.evaluated ||
        facts.counts.changed > facts.corrected_samples ||
        facts.raw_low_samples > facts.counts.evaluated ||
        facts.raw_high_samples > facts.counts.evaluated - facts.raw_low_samples ||
        facts.blended_low_samples > facts.corrected_samples ||
        facts.blended_high_samples > facts.corrected_samples - facts.blended_low_samples ||
        facts.counts.changed <
            facts.corrected_samples - facts.blended_low_samples - facts.blended_high_samples ||
        facts.blended_low_samples > facts.raw_low_samples ||
        facts.blended_high_samples > facts.raw_high_samples) {
        return false;
    }
    switch (facts.reason) {
    case WienerReason::zero_blend:
        return facts.disposition == Disposition::no_change && untouched(facts);
    case WienerReason::no_eligible_samples:
        return facts.disposition == Disposition::no_change && facts.counts.eligible == 0 &&
               untouched(facts);
    case WienerReason::none:
        return facts.disposition == Disposition::applied && facts.counts.changed != 0 &&
               active(facts);
    case WienerReason::no_effect:
        return facts.disposition == Disposition::no_change && facts.counts.changed == 0 &&
               active(facts);
    }
    return false;
}
} // namespace docenhance::methods
