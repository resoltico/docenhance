// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "illumination_gain.hpp"
#include "surface_detail.hpp"
#include "surface_working_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
void observe(SurfaceApplication& application, const IlluminationAdjustment& value) noexcept {
    if (application.evaluated == 0) {
        application.minimum = value.gain;
        application.maximum = value.gain;
    } else {
        application.minimum = std::min(application.minimum, value.gain);
        application.maximum = std::max(application.maximum, value.gain);
    }
    ++application.evaluated;
    application.capped += static_cast<std::uint64_t>(value.capped);
    application.saturated += static_cast<std::uint64_t>(value.saturated);
}
core::Result<void> map_row(Working working, SurfaceMapInput input, std::uint32_t y,
                           SurfaceApplication& application) {
    const auto row = working.luminance.row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    for (std::uint32_t x = 0; x < row.size(); ++x) {
        if (x % surface_poll_interval == 0 &&
            working.scheduler.get().cancellation().requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        const auto background =
            surface_background_value(input.field.grid, input.field.logarithms, {.x = x, .y = y});
        if (!std::isfinite(background) || background <= 0) {
            return core::failure(core::ErrorCode::numerical, "Invalid interpolated log surface");
        }
        if (background > 1 || !input.target_valid) {
            return core::failure(core::ErrorCode::argument,
                                 "Invalid illumination application values");
        }
        auto& destination = row.subspan(x, 1).front();
        const auto adjusted = illumination_adjustment(destination, background, input.gain);
        observe(application, adjusted);
        if (adjusted.target != destination) {
            destination = adjusted.target;
            ++application.changed;
        }
    }
    return {};
}
void reduce(SurfaceFacts& facts, const SurfaceApplication& partial) noexcept {
    if (partial.evaluated == 0) {
        return;
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
} // namespace
core::Result<SurfaceApplication> map_surface_rows(Working working, SurfaceMapInput input,
                                                  std::uint32_t first, std::uint32_t last) {
    SurfaceApplication application;
    for (auto y = first; y < last; ++y) {
        if (auto row = map_row(working, input, y, application); !row) {
            return std::unexpected(std::move(row.error()));
        }
    }
    return application;
}
core::Result<void> map_surface_working(Working working, const Surface& method,
                                       const SurfaceGrid& grid, image::PlaneView<const double> logs,
                                       SurfaceFacts& facts) {
    if (!facts.measurements) {
        return core::failure(core::ErrorCode::invariant, "Surface application lacks measurements");
    }
    const auto p = method.parameters();
    const auto target = facts.measurements->target;
    const SurfaceMapInput input{
        .field = {.grid = grid, .logarithms = logs},
        .gain = {.target = target, .strength = p.strength, .max_gain = p.max_gain},
        .target_valid = std::isfinite(target) && target >= 0 && target <= 1,
    };
    const auto bands = surface_bands(working.luminance.height());
    std::array<SurfaceApplication, surface_bands_max> partials{};
    const auto task = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = surface_band(working.luminance.height(), band);
        auto application = map_surface_rows(working, input, first, last);
        if (!application) {
            return std::unexpected(std::move(application.error()));
        }
        std::span{partials}.subspan(band, 1).front() = *application;
        return {};
    };
    if (auto applied = working.scheduler.get().for_each(bands, exec::WorkRef{task}); !applied) {
        return std::unexpected(std::move(applied.error()));
    }
    for (const auto partial : std::span{partials}.first(bands)) {
        reduce(facts, partial);
    }
    facts.disposition = facts.counts.changed == 0 ? Disposition::no_change : Disposition::applied;
    facts.reason = facts.counts.changed == 0 ? SurfaceReason::no_effect : SurfaceReason::none;
    return {};
}
} // namespace docenhance::methods
