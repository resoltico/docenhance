// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/tvl1_working.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "tvl1_detail.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>

namespace docenhance::methods {
namespace {
struct Tvl1Changes {
    std::uint64_t corrected{};
    std::uint64_t changed{};
};
bool map_row(Working working, std::uint32_t y, const Tvl1& method, const Tvl1Fit& fit,
             Tvl1Changes& changes) noexcept {
    const auto row = working.luminance.row(y);
    const auto input = fit.input.view().row(y);
    const auto primal = fit.primal.view().row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    const auto cancellation = working.scheduler.get().cancellation();
    const auto blend = method.parameters().blend;
    for (std::size_t x = 0; x < row.size(); ++x) {
        constexpr std::size_t interval = 1024;
        if (x % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return false;
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        const auto f = input.subspan(x, 1).front();
        const auto u = primal.subspan(x, 1).front();
        if (u == f) {
            continue;
        }
        ++changes.corrected;
        const auto next = ((1 - blend) * f) + (blend * u);
        if (next != f) {
            row.subspan(x, 1).front() = image::srgb_decode(next);
            ++changes.changed;
        }
    }
    return true;
}
core::Result<void> map_tvl1(const Tvl1& method, Working working, const Tvl1Fit& fit,
                            Tvl1Facts& facts) {
    const auto bands = tvl1_band_count(working.luminance.height());
    std::array<Tvl1Changes, tvl1_row_bands_max> partials{};
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = tvl1_rows(working.luminance.height(), band);
        Tvl1Changes changes;
        for (auto y = first; y < last; ++y) {
            if (!map_row(working, y, method, fit, changes)) {
                return core::cancelled();
            }
        }
        std::span{partials}.subspan(band, 1).front() = changes;
        return {};
    };
    if (auto result = working.scheduler.get().for_each(bands, exec::WorkRef{work}); !result) {
        return std::unexpected(std::move(result.error()));
    }
    facts.counts.evaluated = facts.counts.eligible;
    facts.context_samples = std::uint64_t{working.luminance.width()} * working.luminance.height();
    facts.solver = fit.solution;
    for (const auto changes : std::span{partials}.first(bands)) {
        facts.corrected_samples += changes.corrected;
        facts.counts.changed += changes.changed;
    }
    facts.reason = facts.counts.changed == 0 ? Tvl1Reason::no_effect : Tvl1Reason::none;
    facts.disposition = facts.counts.changed == 0 ? Disposition::no_change : Disposition::applied;
    return {};
}
} // namespace
core::Result<Tvl1Facts> apply(const Tvl1& method, Working working,
                              std::optional<Tvl1Stop>& observed_stop) {
    observed_stop.reset();
    auto counts = eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    Tvl1Facts facts{.counts = *counts};
    if (method.parameters().blend == 0) {
        facts.reason = Tvl1Reason::zero_blend;
        return facts;
    }
    if (facts.counts.eligible == 0) {
        return facts;
    }
    auto fit = fit_tvl1(method, working, observed_stop);
    if (!fit) {
        return std::unexpected(std::move(fit.error()));
    }
    if (auto result = map_tvl1(method, working, *fit, facts); !result) {
        return std::unexpected(std::move(result.error()));
    }
    return facts;
}
} // namespace docenhance::methods
