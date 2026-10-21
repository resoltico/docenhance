// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast_detail.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/clahe.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
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
core::Result<std::uint64_t> map_row(Working working, std::uint32_t y, const Clahe& method,
                                    const ClaheMaps& maps) {
    const auto row = working.luminance.row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    const auto cancellation = working.scheduler.get().cancellation();
    const auto blend = method.parameters().blend;
    std::uint64_t changed = 0;
    for (std::uint32_t x = 0; x < row.size(); ++x) {
        constexpr std::uint32_t interval = 1024;
        if (x % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        auto& destination = row.subspan(x, 1).front();
        const auto f = image::srgb_encode(destination);
        const auto candidate = clahe_candidate(
            f, {.x = x, .y = y},
            {.width = working.luminance.width(), .height = working.luminance.height()}, method,
            maps);
        if (candidate == f) {
            continue;
        }
        const auto next = ((1 - blend) * f) + (blend * candidate);
        if (next != f) {
            destination = image::srgb_decode(next);
            ++changed;
        }
    }
    return changed;
}
core::Result<std::uint64_t> map_clahe(const Clahe& method, Working working, const ClaheMaps& maps) {
    const auto bands =
        std::min(maximum_row_bands, static_cast<std::size_t>(working.luminance.height()));
    std::array<std::uint64_t, maximum_row_bands> partials{};
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto height = std::uint64_t{working.luminance.height()};
        const auto first = static_cast<std::uint32_t>(std::uint64_t{band} * height / bands);
        const auto last = static_cast<std::uint32_t>(std::uint64_t{band + 1} * height / bands);
        std::uint64_t changed = 0;
        for (auto y = first; y < last; ++y) {
            auto row = map_row(working, y, method, maps);
            if (!row) {
                return std::unexpected(std::move(row.error()));
            }
            changed += *row;
        }
        std::span{partials}.subspan(band, 1).front() = changed;
        return {};
    };
    if (auto result = working.scheduler.get().for_each(bands, exec::WorkRef{work}); !result) {
        return std::unexpected(std::move(result.error()));
    }
    std::uint64_t changed = 0;
    for (const auto count : std::span{partials}.first(bands)) {
        changed += count;
    }
    return changed;
}
} // namespace
core::Result<ClaheFacts> apply(const Clahe& method, Working working) {
    auto counts = eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    ClaheFacts facts{.counts = *counts};
    const auto p = method.parameters();
    if (p.blend == 0) {
        facts.reason = ClaheReason::zero_blend;
        return facts;
    }
    if (facts.counts.eligible == 0) {
        return facts;
    }
    if (working.luminance.width() / p.grid_columns < clahe_minimum_tile ||
        working.luminance.height() / p.grid_rows < clahe_minimum_tile) {
        return core::failure(core::ErrorCode::argument,
                             "CLAHE tiles require at least 16 pixels per dimension");
    }
    auto maps = fit_clahe(method, working, facts.counts.eligible);
    if (!maps) {
        return std::unexpected(std::move(maps.error()));
    }
    const auto tiles = p.grid_columns * p.grid_rows;
    for (const auto identity : std::span{maps->identity}.first(tiles)) {
        facts.identity_tiles += static_cast<std::uint32_t>(identity);
    }
    auto changed = map_clahe(method, working, *maps);
    if (!changed) {
        return std::unexpected(std::move(changed.error()));
    }
    facts.counts.evaluated = facts.counts.eligible;
    facts.counts.changed = *changed;
    facts.reason = *changed == 0 ? ClaheReason::no_effect : ClaheReason::none;
    facts.disposition = *changed == 0 ? Disposition::no_change : Disposition::applied;
    return facts;
}
} // namespace docenhance::methods
