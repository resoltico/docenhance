// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

namespace docenhance::methods {
// B03 fixed-threshold binarization. Source and destination have equal non-zero shapes, do not
// overlap, and hold 8-bit grayscale samples. A sample is black iff sample / 255 <= threshold.
// Cancellation can leave the destination partially written; only success makes it usable.
[[nodiscard]] core::Result<void> fixed_threshold(image::PlaneView<const std::uint8_t> source,
                                                 image::PlaneView<std::uint8_t> destination,
                                                 double threshold,
                                                 const core::Cancellation& cancellation = {});
} // namespace docenhance::methods
