// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "require.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
namespace docenhance::tests {
template <typename Owner>
concept BorrowsStatisticPlane = requires(Owner&& owner) { std::forward<Owner>(owner).view(); };
static_assert(sizeof(std::uint32_t) == 4);
static_assert(image::is_sample<std::uint32_t> && image::is_sample<const std::uint32_t>);
static_assert(!image::is_sample<std::int32_t> && !image::is_sample<bool> &&
              !image::is_sample<volatile std::uint32_t>);
static_assert(!std::is_copy_constructible_v<image::Plane<std::uint32_t>>);
static_assert(BorrowsStatisticPlane<image::Plane<std::uint32_t>&> &&
              BorrowsStatisticPlane<const image::Plane<std::uint32_t>&>);
static_assert(!BorrowsStatisticPlane<image::Plane<std::uint32_t>> &&
              !BorrowsStatisticPlane<const image::Plane<std::uint32_t>>);
inline void statistic_plane_cases() {
    core::Budget budget{384};
    {
        auto plane = image::Plane<std::uint32_t>::allocate(budget, 17, 3);
        require(plane && plane->shape().stride == 128 && plane->size_bytes() == 384 &&
                    budget.used() == 384 && budget.peak() == 384,
                "actual uint32 owner charges four-byte samples and aligned rows");
        plane->view().row(2).back() = std::numeric_limits<std::uint32_t>::max();
        auto moved = std::move(*plane);
        require(plane->empty() && plane->width() == 0 && plane->view().empty(),
                "actual uint32 move leaves a ready-to-refuse empty source");
        const auto view = moved.view();
        const auto readonly = view.as_const();
        static_assert(
            std::is_same_v<decltype(readonly), const image::PlaneView<const std::uint32_t>>);
        require(readonly.row(2).back() == std::numeric_limits<std::uint32_t>::max() &&
                    readonly.row_pitch() == 32 && image::overlaps(view, readonly) &&
                    budget.used() == 384,
                "reborrow from final uint32 owner retains full values above65535 and const storage "
                "identity");
    }
    require(budget.used() == 0, "actual uint32 owner refunds its entire charged payload");
    core::Budget short_budget{383};
    require(!image::Plane<std::uint32_t>::allocate(short_budget, 17, 3) && short_budget.used() == 0,
            "one-byte-short real typed uint32 acquisition refuses without charge");
    std::array<std::uint32_t, 9> values{};
    using View = image::PlaneView<std::uint32_t>;
    require(
        View::create(values, {.width = 1, .height = 3, .stride = 16}).has_value() &&
            !View::create(std::span{values}.first(8), {.width = 1, .height = 3, .stride = 16}) &&
            !View::create(values, {.width = 1, .height = 2, .stride = 6}) &&
            !View::create(values, {.width = 2, .height = 1, .stride = 4}),
        "uint32 views keep last-row bounds, four-byte alignment and minimum row restrictions");
    require(!image::SampleDepth::from_bits(32),
            "uint32 statistic admission does not admit32-bit pixel wire depth");
}
} // namespace docenhance::tests
