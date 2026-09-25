// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <bit>
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

// Pixel samples and uint64 integer statistic planes. Anything else is a design
// change, not an instantiation.
template <typename Sample>
inline constexpr bool is_sample = std::is_same_v<std::remove_const_t<Sample>, std::uint8_t> ||
                                  std::is_same_v<std::remove_const_t<Sample>, std::uint16_t> ||
                                  std::is_same_v<std::remove_const_t<Sample>, float> ||
                                  std::is_same_v<std::remove_const_t<Sample>, double> ||
                                  std::is_same_v<std::remove_const_t<Sample>, std::uint64_t>;

struct PlaneShape {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Bytes between consecutive row starts. Owned planes align rows to buffer_alignment;
    // borrowed views need only sample alignment. Never smaller than one row of samples.
    std::size_t stride = 0;
};

// The shape a plane of this size needs, or why it cannot exist.
[[nodiscard]] core::Result<PlaneShape> plane_shape(std::uint32_t width, std::uint32_t height,
                                                   std::size_t sample_size);
// The bytes a shape occupies, checked for overflow.
[[nodiscard]] core::Result<std::size_t> plane_bytes(const PlaneShape& shape);

template <typename Sample> class Plane;

// A borrowed rectangle of samples. Sample may be const; a view never outlives the plane it names.
// It holds a span of the whole plane, so every row is a subspan and no pointer arithmetic is done.
template <typename Sample> class PlaneView {
    static_assert(is_sample<Sample>,
                  "PlaneView supports uint8, uint16, uint64, float and double samples");

  public:
    PlaneView() noexcept = default;
    [[nodiscard]] static core::Result<PlaneView> create(std::span<Sample> samples,
                                                        const PlaneShape& shape) {
        if (shape.width == 0 || shape.height == 0 || shape.stride % sizeof(Sample) != 0) {
            return core::failure(core::ErrorCode::argument,
                                 "A view needs nonzero dimensions and a sample-aligned stride");
        }
        const std::size_t pitch = shape.stride / sizeof(Sample);
        if (pitch < shape.width || samples.size() < shape.width ||
            static_cast<std::size_t>(shape.height - 1) > (samples.size() - shape.width) / pitch) {
            return core::failure(core::ErrorCode::argument,
                                 "The view rectangle exceeds its backing storage");
        }
        return PlaneView{samples, shape};
    }
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
    // Complete backing storage, including row padding. Algorithms use this only for ownership and
    // overlap checks; sample access remains row-based so padding is never interpreted as pixels.
    [[nodiscard]] std::span<Sample> storage() const noexcept {
        return samples_;
    }
    // Samples between row starts. Construction validates that this division is exact.
    [[nodiscard]] std::size_t row_pitch() const noexcept {
        return shape_.stride / sizeof(Sample);
    }
    // The row at y, which the caller must know is inside the plane.
    [[nodiscard]] std::span<Sample> row(std::uint32_t y) const noexcept {
        return samples_.subspan(static_cast<std::size_t>(y) * row_pitch(), shape_.width);
    }

  private:
    template <typename> friend class Plane;
    template <typename> friend class PlaneView;
    PlaneView(std::span<Sample> samples, const PlaneShape& shape) noexcept
        : samples_(samples), shape_(shape) {}
    std::span<Sample> samples_;
    PlaneShape shape_;
};

// Addresses are compared as integers without forming an overflowing end address or comparing
// unrelated pointers. Full backing spans (including padding) must be disjoint for separate roles.
template <typename Left, typename Right>
[[nodiscard]] bool overlaps(PlaneView<Left> left, PlaneView<Right> right) noexcept {
    if (left.empty() || right.empty()) {
        return false;
    }
    const auto first = std::bit_cast<std::uintptr_t>(left.storage().data());
    const auto second = std::bit_cast<std::uintptr_t>(right.storage().data());
    return first <= second ? second - first < left.storage().size_bytes()
                           : first - second < right.storage().size_bytes();
}

// An owning plane. Its memory is charged to the budget that allocated it and refunded when it dies.
template <typename Sample> class Plane {
    static_assert(is_sample<Sample> && !std::is_const_v<Sample>,
                  "Plane owns mutable uint8, uint16, uint64, float or double samples");

  public:
    Plane() noexcept = default;
    Plane(const Plane&) = delete;
    Plane& operator=(const Plane&) = delete;
    Plane(Plane&& other) noexcept
        : buffer_(std::move(other.buffer_)), shape_(std::exchange(other.shape_, {})) {}
    Plane& operator=(Plane&& other) noexcept {
        if (this != &other) {
            buffer_ = std::move(other.buffer_);
            shape_ = std::exchange(other.shape_, {});
        }
        return *this;
    }
    ~Plane() = default;
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
