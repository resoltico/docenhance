// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "require.hpp"
#include "sharpen_reference.hpp"
#include "unsharp_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace docenhance::tests {
struct UnsharpReference {
    std::vector<double> samples;
    methods::UnsharpFacts facts;
};
inline bool unsharp_reference_constant(const std::vector<double>& values,
                                       const SharpenReferenceSettings& settings,
                                       SharpenReferencePosition position, std::int64_t radius) {
    const auto center = values.at((std::size_t{position.y} * settings.width) + position.x);
    for (auto j = -radius; j <= radius; ++j) {
        for (auto i = -radius; i <= radius; ++i) {
            const auto x = sharpen_reflect(std::int64_t{position.x} + i, settings.width);
            const auto y = sharpen_reflect(std::int64_t{position.y} + j, settings.height);
            if (values.at((std::size_t{y} * settings.width) + x) != center) {
                return false;
            }
        }
    }
    return true;
}
inline void unsharp_reference_observe(methods::UnsharpFacts& facts, double raw, double entering) {
    ++facts.counts.evaluated;
    facts.corrected_samples += static_cast<std::uint64_t>(raw != entering);
    facts.clipped_low_samples += static_cast<std::uint64_t>(raw < 0);
    facts.clipped_high_samples += static_cast<std::uint64_t>(raw > 1);
    if (!facts.pre_clamp) {
        facts.pre_clamp = methods::ExcursionRange{.low = raw, .high = raw};
    } else {
        facts.pre_clamp->low = std::min(facts.pre_clamp->low, raw);
        facts.pre_clamp->high = std::max(facts.pre_clamp->high, raw);
    }
}
inline std::vector<double> unsharp_reference_context(UnsharpFixture& fixture) {
    std::vector<double> values;
    const auto view = fixture.luminance.view();
    values.reserve(std::size_t{view.width()} * view.height());
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        const auto row = view.row(y);
        for (const auto sample : row) {
            values.push_back(sharpen_reference_srgb_encode(sample));
        }
    }
    return values;
}
inline void unsharp_reference_row(UnsharpFixture& fixture, std::uint32_t y,
                                  const std::vector<double>& values,
                                  const SharpenReferenceSettings& settings,
                                  UnsharpReference& result) {
    const auto gaussian = sharpen_reference_gaussian(settings.sigma);
    const auto view = fixture.luminance.view();
    for (std::uint32_t x = 0; x < view.width(); ++x) {
        if (fixture.protection.view().row(y).subspan(x, 1).front() != 0) {
            continue;
        }
        const auto f = values.at((std::size_t{y} * view.width()) + x);
        const SharpenReferencePosition position{.x = x, .y = y};
        const auto blurred = unsharp_reference_constant(values, settings, position, gaussian.radius)
                                 ? f
                                 : sharpen_reference_blur(values, settings, position, gaussian);
        const auto d = f - blurred;
        const auto threshold = settings.threshold / 255;
        const auto residual = std::abs(d) <= threshold ? 0 : d - std::copysign(threshold, d);
        const auto raw = f + (settings.amount * residual);
        require(std::isfinite(raw), "independent unsharp RAW is finite before clipping");
        unsharp_reference_observe(result.facts, raw, f);
        const auto candidate = std::clamp(raw, 0.0, 1.0);
        if (candidate != f) {
            result.samples.at((std::size_t{y} * view.row_pitch()) + x) =
                sharpen_reference_srgb_decode(candidate);
            ++result.facts.counts.changed;
        }
    }
}
inline UnsharpReference unsharp_reference(UnsharpFixture& fixture, methods::UnsharpParameters p) {
    UnsharpReference result;
    const auto view = fixture.luminance.view();
    result.samples.assign(view.storage().begin(), view.storage().end());
    const auto protection = fixture.protection.view();
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        const auto mask = protection.row(y);
        for (const auto value : mask) {
            if (value == 0) {
                ++result.facts.counts.eligible;
            } else {
                ++result.facts.counts.protected_samples;
            }
        }
    }
    if (p.amount == 0) {
        result.facts.reason = methods::UnsharpReason::zero_amount;
        return result;
    }
    if (result.facts.counts.eligible == 0) {
        return result;
    }
    result.facts.context_samples = std::uint64_t{view.width()} * view.height();
    const auto values = unsharp_reference_context(fixture);
    const SharpenReferenceSettings settings{
        .width = view.width(),
        .height = view.height(),
        .sigma = p.sigma,
        .amount = p.amount,
        .threshold = p.threshold,
    };
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        unsharp_reference_row(fixture, y, values, settings, result);
    }
    result.facts.reason = result.facts.counts.changed == 0 ? methods::UnsharpReason::no_effect
                                                           : methods::UnsharpReason::none;
    result.facts.disposition = result.facts.counts.changed == 0 ? methods::Disposition::no_change
                                                                : methods::Disposition::applied;
    return result;
}
} // namespace docenhance::tests
