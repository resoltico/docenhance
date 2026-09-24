// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/png.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace docenhance::io {
// Static PNG, decoded without quantization: low-depth grayscale/palette expand to 8 bits;
// 16-bit samples retain network byte order. The span is immutable and borrowed for this call.
[[nodiscard]] core::Result<image::Raster>
decode_png_raster(std::span<const std::uint8_t> bytes, core::Budget& budget,
                  image::ProfilePolicy policy, const core::Cancellation& cancellation = {},
                  PngLimits limits = {});
[[nodiscard]] core::Result<image::Raster>
load_png_raster(const std::string& input, core::Budget& budget, image::ProfilePolicy policy,
                const core::Cancellation& cancellation = {});
// Encode, close, independently reopen and verify all integer rows and metadata, then use
// the same exclusive publication transaction and cancellation cutoff as binary output.
[[nodiscard]] core::Result<std::string>
publish_png_rows(const std::string& output_directory, image::RowSource& rows, core::Budget& budget,
                 const core::Cancellation& cancellation = {});
} // namespace docenhance::io
