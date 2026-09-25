// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "context.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>

namespace docenhance::color {
inline constexpr std::uint32_t conversion_pixels = 4096;
struct ConversionState {
    Context context;
    std::reference_wrapper<const image::Raster> source;
    image::ToneParameters parameters;
    core::Cancellation cancellation;
    image::ConversionReport report;
    core::Buffer profile;
    Profile input_profile;
    Profile linear_profile;
    Transform transform;
    std::optional<double> power_exponent;
    image::Plane<float> input;
    image::Plane<float> linear;
    image::Plane<float> alpha;
    ConversionState(const image::Raster& raster, image::Continuous operation, core::Budget& budget,
                    core::Cancellation control)
        : context(budget), source(raster), parameters(operation.parameters()),
          cancellation(std::move(control)) {}
};
[[nodiscard]] core::Result<void> prepare_conversion(ConversionState& state, core::Budget& budget);
[[nodiscard]] core::Result<void> convert_row(ConversionState& state, std::uint32_t row,
                                             std::span<std::uint8_t> output, image::RowUse use);
[[nodiscard]] core::Result<void> read_linear(ConversionState& state, image::RowRange range,
                                             std::span<double> rgb, image::RowUse use);
} // namespace docenhance::color
