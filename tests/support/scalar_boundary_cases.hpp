// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/raster.hpp"
#include "require.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace docenhance::tests {
inline void quantization_domain_cases() {
    const image::RasterShape shape{
        .width = 1,
        .height = 1,
        .model = image::SampleModel::rgb,
        .depth = image::SampleDepth::byte(),
    };
    const std::array<double, 3> healthy{0.1, 0.3, 0.7};
    std::array<std::uint8_t, 3> output{};
    require(image::quantize_linear(shape, healthy, output).has_value(),
            "unit RGB with matching real output storage is admitted");
    for (const auto invalid : std::array{
             -0.01,
             1.01,
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        for (const auto channel : {0U, 1U, 2U}) {
            auto samples = healthy;
            samples.at(channel) = invalid;
            const auto result = image::quantize_linear(shape, samples, output);
            require(!result && result.error().code == core::ErrorCode::argument,
                    "the real quantization boundary rejects invalid RGB before integer conversion");
        }
    }
}
} // namespace docenhance::tests
