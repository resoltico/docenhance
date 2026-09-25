// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"

#include <cstdint>
#include <span>
#include <string>
namespace docenhance::io {
// Raw 1/8-bit gray mask in already-oriented coordinates. Any alpha must be entirely opaque.
// Semantic color profiles are ignored; structural PNG checks and normal EXIF orientation remain.
[[nodiscard]] core::Result<image::Plane<std::uint8_t>>
decode_protection_png(std::span<const std::uint8_t> bytes, image::Extent extent,
                      core::Budget& budget, const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<image::Plane<std::uint8_t>>
load_protection_png(const std::string& path, image::Extent extent, core::Budget& budget,
                    const core::Cancellation& cancellation = {});
} // namespace docenhance::io
