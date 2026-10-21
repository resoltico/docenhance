// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::codec {
// Raw 1/8-bit gray mask in already-oriented coordinates. Any alpha must be entirely opaque.
// Semantic color profiles are ignored; structural PNG checks and normal EXIF orientation remain.
[[nodiscard]] core::Result<image::Plane<std::uint8_t>>
decode_protection_png(std::span<const std::byte> input, image::Extent extent, core::Budget& budget,
                      const core::Cancellation& cancellation = {});
} // namespace docenhance::codec
