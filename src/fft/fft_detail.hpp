// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
namespace docenhance::fft {
inline constexpr std::uint32_t block_size = 64;
inline constexpr std::size_t checkpoint_samples = 256;
enum class Axis { rows, columns };
[[nodiscard]] inline std::unexpected<core::Error> failed(core::ErrorCode code) noexcept {
    return std::unexpected(core::Error{.code = code, .message = {}});
}
[[nodiscard]] bool valid_extent(image::Extent extent) noexcept;
[[nodiscard]] bool valid_real(image::PlaneView<const double> view) noexcept;
[[nodiscard]] bool overlaps(std::span<const std::byte> a, std::span<const std::byte> b) noexcept;
[[nodiscard]] unsigned native_lanes() noexcept;
[[nodiscard]] core::Result<std::size_t> phase_bytes(image::Extent extent, unsigned workers,
                                                    Axis axis) noexcept;
[[nodiscard]] inline std::size_t blocks(std::uint32_t extent) noexcept {
    return (extent / block_size) + static_cast<std::size_t>(extent % block_size != 0);
}
struct Transform {
    Transform(image::PlaneView<const double> source, image::PlaneView<double> target,
              Spectrum& field, const exec::Scheduler& schedule, bool backward) noexcept
        : input(source), output(target), spectrum(field), scheduler(schedule), inverse(backward) {}

    image::PlaneView<const double> input;
    image::PlaneView<double> output;
    std::reference_wrapper<Spectrum> spectrum;
    std::reference_wrapper<const exec::Scheduler> scheduler;
    bool inverse;
};
[[nodiscard]] core::Result<void> native_rows(const Transform& t, std::uint32_t first,
                                             std::uint32_t count);
[[nodiscard]] core::Result<void> native_columns(const Transform& t, std::uint32_t first,
                                                std::uint32_t count);
[[nodiscard]] core::Result<void> run_phase(const Transform& t, core::Budget& budget, Axis axis);
[[nodiscard]] core::Result<void> copy_spectrum(const Spectrum& from, Spectrum& to,
                                               const core::Cancellation& cancellation);
} // namespace docenhance::fft
