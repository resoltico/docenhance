// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/unsharp.hpp"
#include "docenhance/methods/working.hpp"
#include "sharpen_detail.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>

namespace docenhance::methods {
namespace {
struct UnsharpChanges {
    std::uint64_t changed{};
    std::uint64_t corrected{};
    std::uint64_t low{};
    std::uint64_t high{};
    std::optional<ExcursionRange> range = std::nullopt;
};
void observe(UnsharpChanges& changes, double raw, double entering) noexcept {
    changes.corrected += static_cast<std::uint64_t>(raw != entering);
    changes.low += static_cast<std::uint64_t>(raw < 0);
    changes.high += static_cast<std::uint64_t>(raw > 1);
    if (!changes.range) {
        changes.range = ExcursionRange{.low = raw, .high = raw};
    } else {
        changes.range->low = std::min(changes.range->low, raw);
        changes.range->high = std::max(changes.range->high, raw);
    }
}
bool map_row(Working working, std::uint32_t y, const Unsharp& method,
             image::PlaneView<const double> blur, UnsharpChanges& changes) noexcept {
    const auto row = working.luminance.row(y);
    const auto background = blur.row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    const auto cancellation = working.scheduler.get().cancellation();
    for (std::size_t x = 0; x < row.size(); ++x) {
        constexpr std::size_t interval = 1024;
        if (x % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return false;
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        auto& destination = row.subspan(x, 1).front();
        const auto entering = image::srgb_encode(destination);
        const auto raw = unsharp_candidate(entering, background.subspan(x, 1).front(), method);
        observe(changes, raw, entering);
        const auto candidate = std::clamp(raw, 0.0, 1.0);
        if (candidate != entering) {
            destination = image::srgb_decode(candidate);
            ++changes.changed;
        }
    }
    return true;
}
void reduce(UnsharpFacts& facts, const UnsharpChanges& changes) noexcept {
    facts.counts.changed += changes.changed;
    facts.corrected_samples += changes.corrected;
    facts.clipped_low_samples += changes.low;
    facts.clipped_high_samples += changes.high;
    if (changes.range) {
        if (!facts.pre_clamp) {
            facts.pre_clamp = changes.range;
        } else {
            facts.pre_clamp->low = std::min(facts.pre_clamp->low, changes.range->low);
            facts.pre_clamp->high = std::max(facts.pre_clamp->high, changes.range->high);
        }
    }
}
core::Result<void> map_unsharp(const Unsharp& method, Working working,
                               image::PlaneView<const double> blur, UnsharpFacts& facts) {
    const auto bands = unsharp_band_count(working.luminance.height());
    std::array<UnsharpChanges, unsharp_row_bands_max> partials{};
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = unsharp_rows(working.luminance.height(), band);
        UnsharpChanges changes;
        for (auto y = first; y < last; ++y) {
            if (!map_row(working, y, method, blur, changes)) {
                return core::cancelled();
            }
        }
        std::span{partials}.subspan(band, 1).front() = changes;
        return {};
    };
    if (auto result = working.scheduler.get().for_each(bands, exec::WorkRef{work}); !result) {
        return std::unexpected(std::move(result.error()));
    }
    facts.context_samples = std::uint64_t{working.luminance.width()} * working.luminance.height();
    facts.counts.evaluated = facts.counts.eligible;
    for (const auto& changes : std::span{partials}.first(bands)) {
        reduce(facts, changes);
    }
    facts.reason = facts.counts.changed == 0 ? UnsharpReason::no_effect : UnsharpReason::none;
    facts.disposition = facts.counts.changed == 0 ? Disposition::no_change : Disposition::applied;
    return {};
}
} // namespace
core::Result<UnsharpFacts> apply(const Unsharp& method, Working working) {
    auto counts = eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    UnsharpFacts facts{.counts = *counts};
    if (method.parameters().amount == 0) {
        facts.reason = UnsharpReason::zero_amount;
        return facts;
    }
    if (facts.counts.eligible == 0) {
        return facts;
    }
    auto blur = fit_unsharp(method, working);
    if (!blur) {
        return std::unexpected(std::move(blur.error()));
    }
    if (auto result = map_unsharp(method, working, blur->view().as_const(), facts); !result) {
        return std::unexpected(std::move(result.error()));
    }
    return facts;
}
} // namespace docenhance::methods
