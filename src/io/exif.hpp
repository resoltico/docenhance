// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <span>
namespace docenhance::io {
[[nodiscard]] core::Result<void> parse_exif(std::span<const std::uint8_t> bytes,
                                            image::RasterMetadata& metadata);
}
