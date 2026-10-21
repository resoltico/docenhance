// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/morphology_working.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/working.hpp"
#include "illumination_gain.hpp"
#include "morphology_detail.hpp"
#include "morphology_working_detail.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
struct MorphologyChanges {
    std::uint64_t evaluated{};
    std::uint64_t changed{};
    std::uint64_t capped{};
    std::uint64_t saturated{};
    double minimum = 1;
    double maximum = 1;
};
bool map_row(Working working, std::uint32_t y, image::PlaneView<const double> background,
             IlluminationGain gain, MorphologyChanges& changes) noexcept {
    const auto row = working.luminance.row(y);
    const auto field = background.row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    for (std::size_t x = 0; x < row.size(); ++x) {
        constexpr std::size_t interval = 128;
        if (x % interval == 0 &&
            working.scheduler.get().cancellation().requested(core::Checkpoint::processing)) {
            return false;
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        auto& destination = row.subspan(x, 1).front();
        const auto adjusted =
            illumination_adjustment(destination, field.subspan(x, 1).front(), gain);
        if (changes.evaluated == 0) {
            changes.minimum = adjusted.gain;
            changes.maximum = adjusted.gain;
        } else {
            changes.minimum = std::min(changes.minimum, adjusted.gain);
            changes.maximum = std::max(changes.maximum, adjusted.gain);
        }
        ++changes.evaluated;
        changes.capped += static_cast<std::uint64_t>(adjusted.capped);
        changes.saturated += static_cast<std::uint64_t>(adjusted.saturated);
        if (adjusted.target != destination) {
            destination = adjusted.target;
            ++changes.changed;
        }
    }
    return true;
}
core::Result<void> map(Working working, const Morphology& method, const MorphologyFit& fit,
                       MorphologyFacts& facts) {
    const auto bands = morphology_bands(working.luminance.height());
    std::array<MorphologyChanges, morphology_bands_max> partials{};
    const auto p = method.parameters();
    const IlluminationGain gain{
        .target = fit.analysis.target,
        .strength = p.strength,
        .max_gain = p.max_gain,
    };
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = morphology_lines(working.luminance.height(), band);
        MorphologyChanges partial;
        for (auto y = first; y < last; ++y) {
            if (!map_row(working, y, fit.background.view(), gain, partial)) {
                return core::cancelled();
            }
        }
        std::span{partials}.subspan(band, 1).front() = partial;
        return {};
    };
    if (auto result = working.scheduler.get().for_each(bands, exec::WorkRef{work}); !result) {
        return std::unexpected(std::move(result.error()));
    }
    for (const auto partial : std::span{partials}.first(bands)) {
        if (partial.evaluated == 0) {
            continue;
        }
        if (facts.counts.evaluated == 0) {
            facts.min_gain = partial.minimum;
            facts.max_gain = partial.maximum;
        } else {
            facts.min_gain = std::min(facts.min_gain, partial.minimum);
            facts.max_gain = std::max(facts.max_gain, partial.maximum);
        }
        facts.counts.evaluated += partial.evaluated;
        facts.counts.changed += partial.changed;
        facts.gain_capped_samples += partial.capped;
        facts.saturated_samples += partial.saturated;
    }
    facts.analysis = fit.analysis;
    facts.reason = facts.counts.changed == 0 ? MorphologyReason::no_effect : MorphologyReason::none;
    facts.disposition = facts.counts.changed == 0 ? Disposition::no_change : Disposition::applied;
    return {};
}
} // namespace
core::Result<MorphologyFacts> apply(const Morphology& method, Working working) {
    auto counts = eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    const image::Extent extent{
        .width = working.luminance.width(),
        .height = working.luminance.height(),
    };
    const auto p = method.parameters();
    const auto radius = morphology_radius(extent, method);
    MorphologyFacts facts{
        .counts = *counts,
        .resolved_radius = radius,
        .sigma = morphology_sigma(radius),
        .gaussian_radius = morphology_gaussian_radius(radius),
    };
    if (p.strength == 0) {
        facts.reason = MorphologyReason::zero_strength;
        return facts;
    }
    if (p.max_gain == 1) {
        facts.reason = MorphologyReason::unit_gain;
        return facts;
    }
    if (counts->eligible == 0) {
        return facts;
    }
    if (counts->eligible <
        std::min(morphology_minimum_samples, std::uint64_t{extent.width} * extent.height)) {
        return core::failure(core::ErrorCode::method_inapplicable,
                             "I02 has too few eligible samples");
    }
    auto fit = fit_morphology(method, working, counts->eligible);
    if (!fit) {
        return std::unexpected(std::move(fit.error()));
    }
    if (auto result = map(working, method, *fit, facts); !result) {
        return std::unexpected(std::move(result.error()));
    }
    if (!validate(facts, p, extent)) {
        return core::failure(core::ErrorCode::invariant, "I02 produced invalid completed facts");
    }
    return facts;
}
} // namespace docenhance::methods
