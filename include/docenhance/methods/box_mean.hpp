// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"

#include <cstdint>

namespace docenhance::methods {
// The mean of every (2*radius+1) square window, which is the substrate several planned methods
// share: the blurred term of unsharp masking, the local mean of Sauvola thresholding, and the
// illumination surfaces of the shadow methods. It is a primitive, not a method — nothing is added
// to the advertised catalog by its existence.
//
// The filter is separable, so it runs as two one-dimensional passes through one intermediate
// plane, each pass keeping a running sum instead of re-adding the window at every pixel. Borders
// reflect, on the same REFLECT_101 convention as image::reflect101, so a page's edge is neither
// darkened nor duplicated.
//
// What it guarantees:
//   same answers    The work is divided into tiles of a fixed size, never into one piece per
//                   worker, so the order the sums accumulate in — and therefore every rounded
//                   result — is identical whatever --threads says. Runs are bitwise reproducible.
//   bounded work    Initialization is at most one reflected extent per pass, never proportional
//                   to the radius. The running passes are linear in the plane size.
//   bounded memory  The only allocation is the intermediate plane, charged to the budget. The
//                   column sums live on the stack, so more workers cost no more memory.
//   no exceptions   A plane too large for the budget, a mismatched destination or a window of
//                   radius zero are returned as errors.
//
// Preconditions the caller owns: the samples are finite, and the source and destination are
// different planes. A running sum carries a NaN across the rest of its row, so a decoder validates
// once rather than every kernel paying for it; overlapping storage is rejected here.

// Tiles are this size whatever the machine is: the partition decides the arithmetic, so it may not
// depend on the worker count. Sixty-four columns of sums are 512 bytes, which is stack-sized.
inline constexpr std::uint32_t tile_columns = 64;
inline constexpr std::uint32_t tile_rows = 64;

[[nodiscard]] core::Result<void> box_mean(image::PlaneView<const float> source,
                                          image::PlaneView<float> destination, std::uint32_t radius,
                                          const exec::Scheduler& scheduler, core::Budget& budget);
} // namespace docenhance::methods
