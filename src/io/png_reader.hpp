// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_context.hpp"

#include <cstdint>
namespace docenhance::io {
[[nodiscard]] core::Result<image::Plane<std::uint8_t>>
decode_png(PngContext& context, PngInput& input, PngLimits limits);
} // namespace docenhance::io
