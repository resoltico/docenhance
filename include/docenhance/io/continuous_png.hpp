// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/png.hpp"

#include <cstdint>
#include <span>

namespace docenhance::io {
// Static PNG, decoded without quantization: low-depth grayscale/palette expand to 8 bits;
// 16-bit samples retain network byte order. The span is immutable and borrowed for this call.
[[nodiscard]] core::Result<image::Raster>
decode_png_raster(std::span<const std::uint8_t> bytes, core::Budget& budget,
                  image::ProfilePolicy policy, const core::Cancellation& cancellation = {},
                  PngLimits limits = {});
// Encode into an existing directory, close, reopen and verify every integer row and the metadata.
[[nodiscard]] core::Result<void>
write_verified_png_rows(const BundleSlot& slot, image::RowSource& rows, core::Budget& budget,
                        const core::Cancellation& cancellation = {});
} // namespace docenhance::io
