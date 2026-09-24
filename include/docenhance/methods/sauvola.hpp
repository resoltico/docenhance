// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"

#include <cstddef>
#include <cstdint>

namespace docenhance::methods {
// Fixed partition, not a function of worker count. See docs/binarization.md for B02's definition.
inline constexpr std::uint32_t sauvola_strip_columns = 4096;
struct SauvolaWorkspace {
    image::PlaneShape shape;
    unsigned slots = 0;
    std::size_t bytes = 0;
};
[[nodiscard]] core::Result<SauvolaWorkspace>
sauvola_workspace(std::uint32_t width, const Sauvola& method, unsigned workers);
// All scratch is acquired before writing destination samples. REFLECT_101, population variance,
// exact uint64 moments; no full-page float statistics. Samples/outputs must be disjoint.
[[nodiscard]] core::Result<void> sauvola(image::PlaneView<const std::uint8_t> source,
                                         image::PlaneView<std::uint8_t> destination,
                                         const Sauvola& method, BinarizationContext context);
} // namespace docenhance::methods
