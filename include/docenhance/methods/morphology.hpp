// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"

#include <cstdint>
#include <span>
namespace docenhance::methods {
struct ExtremaPass {
    std::uint32_t radius{};
    bool horizontal = true;
    bool dilation = true;
    std::uint32_t first_line{};
    std::uint32_t last_line{};
};
// Producer-admitted finite Unit logical input; cold full-role storage and nonempty axis
// interval admission precede cancellation. Each simultaneous pass needs its own queue.
[[nodiscard]] core::Result<void> extrema_pass(image::PlaneView<const double> input,
                                              image::PlaneView<double> output,
                                              std::span<std::uint64_t> queue, ExtremaPass pass,
                                              const core::Cancellation& cancellation = {});
[[nodiscard]] std::uint32_t morphology_radius(image::Extent extent,
                                              const Morphology& method) noexcept;
} // namespace docenhance::methods
