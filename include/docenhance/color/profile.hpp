// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <cstddef>
#include <span>
namespace docenhance::color {
// Validate canonical output ICC bytes using the same representation definition as the writer,
// without transforming pixels. An empty profile remains valid for binary output.
[[nodiscard]] core::Result<void> validate_output_profile(image::RasterShape shape,
                                                         std::span<const std::byte> bytes,
                                                         core::Budget& budget,
                                                         const core::Cancellation& cancellation);
} // namespace docenhance::color
