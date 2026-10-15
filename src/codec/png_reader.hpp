// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/codec/png.hpp"
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "png_context.hpp"

#include <cstdint>
namespace docenhance::codec {
[[nodiscard]] core::Result<image::Plane<std::uint8_t>>
decode_png(PngContext& context, PngInput& input, PngLimits limits);
} // namespace docenhance::codec
