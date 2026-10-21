// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "fft_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
namespace docenhance::fft {
static_assert(sizeof(Complex) == 2 * sizeof(double));
static_assert(core::buffer_alignment % alignof(Complex) == 0);
static_assert(std::is_trivially_copy_constructible_v<Complex> &&
              std::is_trivially_destructible_v<Complex>);
Spectrum::Spectrum(core::Buffer buffer, image::Extent extent, image::PlaneShape shape) noexcept
    : buffer_(std::move(buffer)), extent_(extent), shape_(shape) {}
Spectrum::Spectrum(Spectrum&& other) noexcept
    : buffer_(std::move(other.buffer_)), extent_(std::exchange(other.extent_, {})),
      shape_(std::exchange(other.shape_, {})) {}
Spectrum& Spectrum::operator=(Spectrum&& other) noexcept {
    if (this != &other) {
        buffer_ = std::move(other.buffer_);
        extent_ = std::exchange(other.extent_, {});
        shape_ = std::exchange(other.shape_, {});
    }
    return *this;
}
std::span<Complex> Spectrum::storage() & noexcept {
    return samples_of(*this);
}
std::span<const Complex> Spectrum::storage() const& noexcept {
    return samples_of(*this);
}
std::span<Complex> Spectrum::row(std::uint32_t y) & noexcept {
    return storage().subspan(std::size_t{y} * (shape_.stride / sizeof(Complex)), shape_.width);
}
std::span<const Complex> Spectrum::row(std::uint32_t y) const& noexcept {
    return storage().subspan(std::size_t{y} * (shape_.stride / sizeof(Complex)), shape_.width);
}
core::Result<Spectrum> Spectrum::allocate(core::Budget& budget, image::Extent extent,
                                          const core::Cancellation& cancellation) {
    if (!valid_extent(extent)) {
        return failed(core::ErrorCode::argument);
    }
    auto shape = image::plane_shape((extent.width / 2) + 1, extent.height, sizeof(Complex));
    if (!shape) {
        return std::unexpected(std::move(shape.error()));
    }
    auto bytes = image::plane_bytes(*shape);
    if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
    }
    if (*bytes > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
        return failed(core::ErrorCode::resource);
    }
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return failed(core::ErrorCode::cancelled);
    }
    auto buffer = budget.allocate(*bytes);
    if (!buffer) {
        return std::unexpected(std::move(buffer.error()));
    }
    Spectrum result{std::move(*buffer), extent, *shape};
    const auto data = result.storage();
    // Aligned operator-new backing permits this implicit-lifetime array; every element,
    // including padding, is explicitly constructed before numerical use or returned borrow.
    for (std::size_t index = 0; index < data.size(); ++index) {
        if (index % checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::initialization)) {
            return failed(core::ErrorCode::cancelled);
        }
        std::construct_at(data.subspan(index, 1).data(), 0.0, 0.0);
    }
    return result;
}
} // namespace docenhance::fft
