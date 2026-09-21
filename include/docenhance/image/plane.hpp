// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace docenhance::image {
// One channel of one page. A plane owns its memory through core::Buffer, so it is move-only and no
// operation copies a page by accident; algorithms take views instead, which own nothing.
//
// Every size is computed with checked arithmetic before anything is allocated: width * height *
// sample size overflows 32-bit and even 64-bit arithmetic for inputs a caller may legitimately
// ask about, and an overflowed size is a much worse failure than a refused one.

// The sample types a decoder can produce and a kernel can consume. Anything else is a design
// change, not an instantiation.
template <typename Sample>
inline constexpr bool is_sample = std::is_same_v<std::remove_const_t<Sample>, std::uint8_t> ||
                                  std::is_same_v<std::remove_const_t<Sample>, std::uint16_t> ||
                                  std::is_same_v<std::remove_const_t<Sample>, float>;

struct PlaneShape {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Bytes between the first samples of consecutive rows. Always a multiple of the buffer
    // alignment, so every row starts aligned, and never smaller than one row of samples.
    std::size_t stride = 0;
};

// The shape a plane of this size needs, or why it cannot exist.
[[nodiscard]] core::Result<PlaneShape> plane_shape(std::uint32_t width, std::uint32_t height,
                                                   std::size_t sample_size);
// The bytes a shape occupies, checked for overflow.
[[nodiscard]] core::Result<std::size_t> plane_bytes(const PlaneShape& shape);

// A borrowed rectangle of samples. Sample may be const; a view never outlives the plane it names.
// It holds a span of the whole plane, so every row is a subspan and no pointer arithmetic is done.
template <typename Sample> class PlaneView {
    static_assert(is_sample<Sample>, "PlaneView supports uint8, uint16 and float samples");

  public:
    PlaneView() noexcept = default;
    PlaneView(std::span<Sample> samples, const PlaneShape& shape) noexcept
        : samples_(samples), shape_(shape) {}
    // A mutable view can be read-only, never the reverse. Spelled out at the call site so that
    // handing a kernel write access is always visible.
    [[nodiscard]] PlaneView<const Sample> as_const() const noexcept
        requires(!std::is_const_v<Sample>)
    {
        return {std::span<const Sample>{samples_}, shape_};
    }

    [[nodiscard]] const PlaneShape& shape() const noexcept {
        return shape_;
    }
    [[nodiscard]] std::uint32_t width() const noexcept {
        return shape_.width;
    }
    [[nodiscard]] std::uint32_t height() const noexcept {
        return shape_.height;
    }
    [[nodiscard]] bool empty() const noexcept {
        return samples_.empty();
    }
    // Samples between the starts of consecutive rows. The stride is a multiple of the buffer
    // alignment, which is a multiple of every supported sample size, so this division is exact.
    [[nodiscard]] std::size_t row_pitch() const noexcept {
        return shape_.stride / sizeof(Sample);
    }
    // The row at y, which the caller must know is inside the plane.
    [[nodiscard]] std::span<Sample> row(std::uint32_t y) const noexcept {
        return samples_.subspan(static_cast<std::size_t>(y) * row_pitch(), shape_.width);
    }

  private:
    std::span<Sample> samples_;
    PlaneShape shape_;
};

// An owning plane. Its memory is charged to the budget that allocated it and refunded when it dies.
template <typename Sample> class Plane {
    static_assert(is_sample<Sample> && !std::is_const_v<Sample>,
                  "Plane owns mutable uint8, uint16 or float samples");

  public:
    Plane() noexcept = default;
    [[nodiscard]] static core::Result<Plane> allocate(core::Budget& budget, std::uint32_t width,
                                                      std::uint32_t height) {
        auto shape = plane_shape(width, height, sizeof(Sample));
        if (!shape) {
            return core::failure(shape.error().code, shape.error().message);
        }
        auto bytes = plane_bytes(*shape);
        if (!bytes) {
            return core::failure(bytes.error().code, bytes.error().message);
        }
        auto buffer = budget.allocate(*bytes);
        if (!buffer) {
            return core::failure(buffer.error().code, buffer.error().message);
        }
        return Plane{std::move(*buffer), *shape};
    }

    [[nodiscard]] const PlaneShape& shape() const noexcept {
        return shape_;
    }
    [[nodiscard]] std::uint32_t width() const noexcept {
        return shape_.width;
    }
    [[nodiscard]] std::uint32_t height() const noexcept {
        return shape_.height;
    }
    [[nodiscard]] std::size_t size_bytes() const noexcept {
        return buffer_.size();
    }
    [[nodiscard]] bool empty() const noexcept {
        return buffer_.empty();
    }
    [[nodiscard]] PlaneView<Sample> view() noexcept {
        return {samples(), shape_};
    }
    [[nodiscard]] PlaneView<const Sample> view() const noexcept {
        return {samples(), shape_};
    }

  private:
    // The single place where raw storage becomes samples. The buffer is over-aligned for every
    // supported sample type, the types are implicit-lifetime, and the span covers whole rows only.
    template <typename Self> [[nodiscard]] static auto samples_of(Self& self) noexcept {
        using Byte = std::conditional_t<std::is_const_v<Self>, const std::byte, std::byte>;
        using Element = std::conditional_t<std::is_const_v<Self>, const Sample, Sample>;
        const std::span<Byte> bytes = self.buffer_.bytes();
        return std::span<Element>{
            reinterpret_cast<Element*>(bytes.data()), // NOLINT(*-reinterpret-cast)
            bytes.size() / sizeof(Sample),
        };
    }
    [[nodiscard]] std::span<Sample> samples() noexcept {
        return samples_of(*this);
    }
    [[nodiscard]] std::span<const Sample> samples() const noexcept {
        return samples_of(*this);
    }

    Plane(core::Buffer buffer, const PlaneShape& shape) noexcept
        : buffer_(std::move(buffer)), shape_(shape) {}
    core::Buffer buffer_;
    PlaneShape shape_;
};
} // namespace docenhance::image
