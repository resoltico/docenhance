// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "png_context.hpp"

#include <cstdint>

namespace docenhance::codec {
inline constexpr const char* output_profile_name = "DocEnhance";
void install_png_writer(PngContext& context);
[[nodiscard]] bool output_inventory(PngContext& context, const PngReadStream& stream,
                                    const core::Cancellation& cancellation);
} // namespace docenhance::codec
