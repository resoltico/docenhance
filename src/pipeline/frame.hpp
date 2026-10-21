// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/working.hpp"
#include "docenhance/pipeline/render.hpp"

#include <memory>
#include <optional>

namespace docenhance::pipeline {
// Concrete lifetimes of this one fixed pipeline. Source/interpreter survive only preserve-color
// reconstruction; method scratch dies at each joined call and never enters Rendering.
struct Frame {
    image::Raster source;
    std::unique_ptr<color::Interpretation> interpreter;
    image::Plane<double> luminance;
    image::Plane<std::uint8_t> protection;
    std::optional<methods::ResolvedPsf> psf;
    Rendering rendering;
    Failure failure;
};
[[nodiscard]] core::Result<void> prepare_frame(Input& input, const methods::Plan& plan,
                                               Frame& frame, core::Budget& budget,
                                               const exec::Scheduler& scheduler);
[[nodiscard]] core::Result<void> prepare_mask(Input& input, const methods::Plan& plan, Frame& frame,
                                              core::Budget& budget,
                                              const exec::Scheduler& scheduler);
[[nodiscard]] core::Result<void> resolve_psf(Input& input, const methods::Plan& plan, Frame& frame,
                                             core::Budget& budget,
                                             const exec::Scheduler& scheduler);
[[nodiscard]] core::Result<void> apply_stages(Input& input, const methods::Plan& plan, Frame& frame,
                                              core::Budget& budget,
                                              const exec::Scheduler& scheduler);
[[nodiscard]] core::Result<void> finish_output(const methods::Plan& plan, Frame& frame,
                                               core::Budget& budget,
                                               const exec::Scheduler& scheduler);
} // namespace docenhance::pipeline
