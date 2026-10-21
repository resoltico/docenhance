// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/working.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::methods {
inline constexpr std::uint32_t restoration_band_rows = 64;
inline constexpr std::size_t restoration_checkpoint_samples = 256;
struct RestorationGeometry {
    std::uint32_t guard;
    std::uint32_t width;
    std::uint32_t height;
};
struct RestorationBand {
    std::uint32_t first;
    std::uint32_t rows;
};
[[nodiscard]] inline std::uint32_t restoration_bands(std::uint32_t height) noexcept {
    return (height / restoration_band_rows) +
           static_cast<std::uint32_t>(height % restoration_band_rows != 0);
}
[[nodiscard]] RestorationBand restoration_band(std::uint32_t height, std::size_t index) noexcept;
[[nodiscard]] core::Result<RestorationGeometry> restoration_geometry(Working working,
                                                                     const ResolvedPsf& psf);
[[nodiscard]] core::Result<PsfFacts> restoration_psf_facts(const Wiener& method,
                                                           const ResolvedPsf& psf);
[[nodiscard]] core::Result<double> restoration_mean(Working working, image::PlaneView<double> frame,
                                                    RestorationGeometry geometry);
[[nodiscard]] core::Result<void> restoration_center(Working working, image::PlaneView<double> frame,
                                                    double mean);
[[nodiscard]] core::Result<void> restoration_kernel(Working working, image::PlaneView<double> frame,
                                                    const ResolvedPsf& psf);
[[nodiscard]] core::Result<void> restoration_filter(std::span<std::complex<double>> signal,
                                                    std::span<const std::complex<double>> kernel,
                                                    image::PlaneShape shape, const Wiener& method,
                                                    const exec::Scheduler& scheduler);
[[nodiscard]] core::Result<WienerFacts> restoration_map(Working working, const Wiener& method,
                                                        image::PlaneView<const double> frame,
                                                        RestorationTransform transform,
                                                        WienerFacts facts);
} // namespace docenhance::methods
