// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/levels.hpp"
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
constexpr std::size_t maximum_row_bands = 64;
constexpr std::size_t checkpoint_samples = 1024;
core::Result<void> gather_row(Working working, std::uint32_t y, std::span<double> samples,
                              std::size_t& cursor, const core::Cancellation& cancellation) {
    const auto row = working.luminance.row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    for (std::size_t x = 0; x < row.size(); ++x) {
        if (x % checkpoint_samples == 0 && cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        if (cursor == samples.size()) {
            return core::failure(core::ErrorCode::invariant, "Levels eligibility changed");
        }
        samples.subspan(cursor++, 1).front() = image::srgb_encode(row.subspan(x, 1).front());
    }
    return {};
}
core::Result<LevelsRange> fit_levels(const Levels& method, Working working,
                                     std::uint64_t eligible) {
    const auto cancellation = working.scheduler.get().cancellation();
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto copy = image::Plane<double>::allocate(working.budget.get(),
                                               static_cast<std::uint32_t>(eligible), 1);
    if (!copy) {
        return std::unexpected(std::move(copy.error()));
    }
    const auto samples = copy->view().row(0);
    std::size_t cursor = 0;
    const auto gather = [&](std::size_t) -> core::Result<void> {
        for (std::uint32_t y = 0; y < working.luminance.height(); ++y) {
            if (auto row = gather_row(working, y, samples, cursor, cancellation); !row) {
                return row;
            }
        }
        return {};
    };
    if (auto gathered = working.scheduler.get().for_each(1, exec::WorkRef{gather}); !gathered) {
        return std::unexpected(std::move(gathered.error()));
    }
    if (cursor != samples.size()) {
        return core::failure(core::ErrorCode::invariant, "Incomplete Levels measurements");
    }
    if (auto sorted = image::sort_samples(samples, cancellation); !sorted) {
        return std::unexpected(std::move(sorted.error()));
    }
    const auto parameters = method.parameters();
    constexpr double percent_scale = 100;
    auto low = image::nearest_rank_index(samples.size(), parameters.low / percent_scale);
    auto high = image::nearest_rank_index(samples.size(), parameters.high / percent_scale);
    if (!low) {
        return std::unexpected(std::move(low.error()));
    }
    if (!high) {
        return std::unexpected(std::move(high.error()));
    }
    return LevelsRange{
        .low = samples.subspan(*low, 1).front(),
        .high = samples.subspan(*high, 1).front(),
    };
}
struct LevelsChanges {
    std::uint64_t changed = 0;
    std::uint64_t low = 0;
    std::uint64_t high = 0;
};
bool map_row(Working working, std::uint32_t y, const Levels& method, LevelsRange range,
             LevelsChanges& changes) noexcept {
    const auto row = working.luminance.row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    const auto cancellation = working.scheduler.get().cancellation();
    const auto blend = method.parameters().blend;
    for (std::size_t x = 0; x < row.size(); ++x) {
        if (x % checkpoint_samples == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return false;
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        auto& destination = row.subspan(x, 1).front();
        const double entering = image::srgb_encode(destination);
        changes.low += static_cast<std::uint64_t>(entering < range.low);
        changes.high += static_cast<std::uint64_t>(entering > range.high);
        const double candidate = levels_candidate(entering, range);
        if (candidate == entering) {
            continue;
        }
        const double next = ((1 - blend) * entering) + (blend * candidate);
        if (next != entering) {
            destination = image::srgb_decode(next);
            ++changes.changed;
        }
    }
    return true;
}
core::Result<void> map_levels(const Levels& method, Working working, LevelsRange range,
                              LevelsFacts& facts) {
    const auto bands =
        std::min(maximum_row_bands, static_cast<std::size_t>(working.luminance.height()));
    std::array<LevelsChanges, maximum_row_bands> partials{};
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto height = std::uint64_t{working.luminance.height()};
        const auto first = static_cast<std::uint32_t>(std::uint64_t{band} * height / bands);
        const auto last = static_cast<std::uint32_t>(std::uint64_t{band + 1} * height / bands);
        LevelsChanges changes;
        for (auto y = first; y < last; ++y) {
            if (!map_row(working, y, method, range, changes)) {
                return core::cancelled();
            }
        }
        std::span{partials}.subspan(band, 1).front() = changes;
        return {};
    };
    if (auto mapped = working.scheduler.get().for_each(bands, exec::WorkRef{work}); !mapped) {
        return std::unexpected(std::move(mapped.error()));
    }
    facts.counts.evaluated = facts.counts.eligible;
    for (const auto changes : std::span{partials}.first(bands)) {
        facts.counts.changed += changes.changed;
        facts.clipped_low_samples += changes.low;
        facts.clipped_high_samples += changes.high;
    }
    facts.reason = facts.counts.changed == 0 ? LevelsReason::no_effect : LevelsReason::none;
    facts.disposition = facts.counts.changed == 0 ? Disposition::no_change : Disposition::applied;
    return {};
}
} // namespace
core::Result<LevelsFacts> apply(const Levels& method, Working working) {
    auto counts = eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    LevelsFacts facts{.counts = *counts};
    if (method.parameters().blend == 0) {
        facts.reason = LevelsReason::zero_blend;
        return facts;
    }
    if (facts.counts.eligible == 0) {
        return facts;
    }
    auto range = fit_levels(method, working, facts.counts.eligible);
    if (!range) {
        return std::unexpected(std::move(range.error()));
    }
    facts.levels = *range;
    if (range->high - range->low < levels_minimum_range) {
        facts.reason = LevelsReason::insufficient_dynamic_range;
        return facts;
    }
    if (auto mapped = map_levels(method, working, *range, facts); !mapped) {
        return std::unexpected(std::move(mapped.error()));
    }
    return facts;
}
} // namespace docenhance::methods
