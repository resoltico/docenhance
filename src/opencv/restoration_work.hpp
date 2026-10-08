// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/opencv/restoration.hpp"

#include <cstdint>
namespace docenhance::opencv {
struct RestorationWork {
    image::Plane<double> candidate;
    image::Plane<double> transfer;
    image::Plane<float> input;
    image::Plane<float> kernel;
    image::Plane<float> spectrum;
    image::Plane<float> kernel_spectrum;
    core::Reservation native;
};
[[nodiscard]] core::Result<RestorationWork> restoration_work(image::Extent extent,
                                                             const methods::ResolvedPsf& psf,
                                                             const RestorationExecution& execution);
[[nodiscard]] core::Result<void> restoration_gather(image::LinearSource& source,
                                                    RestorationWork& work,
                                                    const RestorationExecution& execution);
[[nodiscard]] core::Result<void> restoration_pad(RestorationWork& work,
                                                 const methods::ResolvedPsf& psf,
                                                 const RestorationExecution& execution);
[[nodiscard]] core::Result<void> restoration_transforms(RestorationWork& work,
                                                        const methods::Wiener& method,
                                                        const RestorationExecution& execution);
} // namespace docenhance::opencv
