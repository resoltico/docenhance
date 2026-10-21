// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/working.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/report_values.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace docenhance::methods {
namespace {
core::Result<void> count_mask_row(std::span<const std::uint8_t> mask, PixelCounts& counts,
                                  const core::Cancellation& cancellation) {
    constexpr std::size_t checkpoint_samples = 1024;
    std::size_t offset = 0;
    for (const auto sample : mask) {
        if (offset % checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        if (sample != 0) {
            --counts.eligible;
            ++counts.protected_samples;
        }
        ++offset;
    }
    return {};
}
} // namespace
core::Result<void> validate_working_views(Working working) {
    const auto plane = working.luminance;
    const auto mask = working.protection;
    const auto pixels = std::uint64_t{plane.width()} * plane.height();
    if (plane.empty() || pixels == 0 || pixels > image::source_pixels_max) {
        return core::failure(core::ErrorCode::argument, "Invalid working plane extent");
    }
    if (!mask.empty() && (mask.width() != plane.width() || mask.height() != plane.height() ||
                          image::overlaps(plane, mask))) {
        return core::failure(core::ErrorCode::argument, "Invalid working protection view");
    }
    return {};
}

core::Result<PixelCounts> eligibility(Working working) {
    if (auto valid = validate_working_views(working); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    const auto cancellation = working.scheduler.get().cancellation();
    if (cancellation.requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    const auto plane = working.luminance;
    PixelCounts result{.eligible = std::uint64_t{plane.width()} * plane.height()};
    if (working.protection.empty()) {
        return result;
    }
    const auto count = [&](std::size_t) -> core::Result<void> {
        for (std::uint32_t y = 0; y < plane.height(); ++y) {
            if (auto row = count_mask_row(working.protection.row(y), result, cancellation); !row) {
                return row;
            }
        }
        return {};
    };
    // A single fixed row-major reduction runs inline, including algebraic no-ops, without
    // workspace or worker-control allocation. The method's candidate passes use their own bands.
    if (auto counted = working.scheduler.get().for_each(1, exec::WorkRef{count}); !counted) {
        return std::unexpected(std::move(counted.error()));
    }
    return result;
}
} // namespace docenhance::methods
