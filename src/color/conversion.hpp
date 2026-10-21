// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "context.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace docenhance::color {
inline constexpr std::uint32_t conversion_pixels = image::linear_block_pixels;
struct InterpretationReaderState;
struct InterpretationState {
    Context context;
    std::reference_wrapper<const image::Raster> source;
    image::InterpretationPolicy parameters;
    core::Cancellation cancellation;
    image::InterpretationReport report;
    Profile input_profile;
    Profile linear_profile;
    Transform initial_transform;
    bool native = false;
    std::optional<double> power_exponent;
    std::atomic_flag reader_guard;
    std::vector<std::unique_ptr<InterpretationReaderState>> readers;
    InterpretationState(const image::Raster& raster, image::InterpretationPolicy policy,
                        core::Budget& budget, core::Cancellation control);
};
struct InterpretationReaderState {
    std::reference_wrapper<InterpretationState> run;
    bool leased = false;
    Transform transform;
    image::Plane<double> input;
    image::Plane<double> linear;
    image::Plane<double> alpha;
    explicit InterpretationReaderState(InterpretationState& owner) noexcept : run(owner) {}
};
inline InterpretationState::InterpretationState(const image::Raster& raster,
                                                image::InterpretationPolicy policy,
                                                core::Budget& budget, core::Cancellation control)
    : context(budget), source(raster), parameters(policy), cancellation(std::move(control)) {}
[[nodiscard]] core::Result<void> prepare_interpretation(InterpretationState& state);
[[nodiscard]] core::Result<void> prepare_readers(InterpretationState& state, core::Budget& budget,
                                                 unsigned workers);
[[nodiscard]] core::Result<image::BlockObservation>
read_interpretation(InterpretationReaderState& state, std::uint32_t row, std::uint32_t first,
                    std::span<double> output);
} // namespace docenhance::color
