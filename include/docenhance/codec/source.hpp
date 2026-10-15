// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"

#include <cstdint>
#include <span>
namespace docenhance::codec {
struct DecodedRaster {
    image::Raster raster;
    image::SourceFacts description;
};
struct DecodedImage {
    image::Plane<std::uint8_t> image;
    image::PngFacts description;
};
[[nodiscard]] core::Result<DecodedRaster> decode_source(std::span<const std::uint8_t> bytes,
                                                        core::Budget& budget,
                                                        image::ProfilePolicy policy,
                                                        const core::Cancellation& cancellation);
[[nodiscard]] core::Result<DecodedImage>
decode_binary_source(std::span<const std::uint8_t> bytes, core::Budget& budget,
                     const core::Cancellation& cancellation);
} // namespace docenhance::codec
