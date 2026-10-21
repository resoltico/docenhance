// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/gamma.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/working.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace docenhance::methods {
namespace {
// Fixed method metadata and partition, independent of workers and available payload budget.
constexpr std::size_t maximum_row_bands = 64;
bool map_row(std::span<double> luminance, std::span<const std::uint8_t> protection,
             const Gamma& method, const core::Cancellation& cancellation,
             std::uint64_t& changed) noexcept {
    const auto blend = method.parameters().blend;
    constexpr std::size_t checkpoint_samples = 1024;
    for (std::size_t x = 0; x < luminance.size(); ++x) {
        if (x % checkpoint_samples == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return false;
        }
        if (!protection.empty() && protection.subspan(x, 1).front() != 0) {
            continue;
        }
        auto& destination = luminance.subspan(x, 1).front();
        const double entering = image::srgb_encode(destination);
        const double candidate = gamma_candidate(entering, method);
        if (candidate == entering) {
            continue;
        }
        const double next = ((1 - blend) * entering) + (blend * candidate);
        if (next != entering) {
            destination = image::srgb_decode(next);
            ++changed;
        }
    }
    return true;
}
} // namespace
core::Result<GammaFacts> apply(const Gamma& method, Working working) {
    auto counts = eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    GammaFacts facts{.counts = *counts};
    const auto parameters = method.parameters();
    if (parameters.blend == 0) {
        facts.reason = GammaReason::zero_blend;
        return facts;
    }
    if (facts.counts.eligible == 0) {
        return facts;
    }
    if (parameters.gamma == 1) {
        facts.reason = GammaReason::identity_gamma;
        return facts;
    }
    const auto bands =
        std::min(maximum_row_bands, static_cast<std::size_t>(working.luminance.height()));
    std::array<std::uint64_t, maximum_row_bands> changes{};
    const auto cancellation = working.scheduler.get().cancellation();
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto height = std::uint64_t{working.luminance.height()};
        const auto first = static_cast<std::uint32_t>(std::uint64_t{band} * height / bands);
        const auto last = static_cast<std::uint32_t>(std::uint64_t{band + 1} * height / bands);
        std::uint64_t changed = 0;
        for (auto row = first; row < last; ++row) {
            const auto protection = working.protection.empty() ? std::span<const std::uint8_t>{}
                                                               : working.protection.row(row);
            if (!map_row(working.luminance.row(row), protection, method, cancellation, changed)) {
                return core::cancelled();
            }
        }
        std::span{changes}.subspan(band, 1).front() = changed;
        return {};
    };
    if (auto processed = working.scheduler.get().for_each(bands, exec::WorkRef{work}); !processed) {
        return std::unexpected(std::move(processed.error()));
    }
    facts.counts.evaluated = facts.counts.eligible;
    for (const auto changed : std::span{changes}.first(bands)) {
        facts.counts.changed += changed;
    }
    facts.reason = facts.counts.changed == 0 ? GammaReason::no_effect : GammaReason::none;
    facts.disposition = facts.counts.changed == 0 ? Disposition::no_change : Disposition::applied;
    return facts;
}
} // namespace docenhance::methods
