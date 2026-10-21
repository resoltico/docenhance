// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/report_values.hpp"

#include <cstdint>
#include <functional>

namespace docenhance::methods {
// The producer validates finite unit luminance once before any method runs. These views borrow
// the pipeline's frame-F storage until all synchronous scheduler work has joined.
struct Working {
    Working(image::PlaneView<double> plane, image::PlaneView<const std::uint8_t> mask,
            core::Budget& ledger, const exec::Scheduler& schedule) noexcept
        : luminance(plane), protection(mask), budget(ledger), scheduler(schedule) {}
    Working(image::PlaneView<double>, image::PlaneView<const std::uint8_t>, core::Budget&,
            exec::Scheduler&&) = delete;
    Working(image::PlaneView<double>, image::PlaneView<const std::uint8_t>, core::Budget&,
            const exec::Scheduler&&) = delete;
    image::PlaneView<double> luminance;
    image::PlaneView<const std::uint8_t> protection;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const exec::Scheduler> scheduler;
};
// Geometry and disjoint role storage only; this does not rescan validated luminance samples.
[[nodiscard]] core::Result<void> validate_working_views(Working working);
// Nonzero mask samples protect destinations; they remain neighborhood context for the methods.
[[nodiscard]] core::Result<PixelCounts> eligibility(Working working);
} // namespace docenhance::methods
