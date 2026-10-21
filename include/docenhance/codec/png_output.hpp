// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/codec/encoded_png.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace docenhance::codec {
// Continuous samples are network-order integer bytes. PNG1 borrows one logical byte (0/1) per
// pixel, never an expanded 0/255 representation. View width excludes caller row padding.
struct PngImage {
    image::PngShape shape;
    image::PlaneView<const std::uint8_t> pixels;
    std::span<const std::byte> profile;
    std::optional<image::Resolution> resolution;
};
[[nodiscard]] core::Result<EncodedPng> encode_png(PngImage image, core::Budget& budget,
                                                  const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<void> compare_png(std::span<const std::byte> bytes, PngImage intended,
                                             core::Budget& budget,
                                             const core::Cancellation& cancellation = {});
} // namespace docenhance::codec
