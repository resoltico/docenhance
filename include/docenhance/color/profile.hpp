// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"

#include <cstddef>
#include <span>
namespace docenhance::color {
// Called only at the late output boundary, after source/interpreter/working-plane destruction.
// The returned charged bytes retain their full capacity through encoding and independent compare.
[[nodiscard]] core::Result<core::Buffer>
make_output_profile(core::Budget& budget, bool gray, const core::Cancellation& cancellation = {});
// Validate canonical output ICC bytes using the same representation definition as the writer,
// without transforming pixels. An empty profile remains valid for binary output.
[[nodiscard]] core::Result<void> validate_output_profile(image::PngShape shape,
                                                         std::span<const std::byte> bytes,
                                                         core::Budget& budget,
                                                         const core::Cancellation& cancellation);
} // namespace docenhance::color
