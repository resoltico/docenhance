// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
namespace docenhance::io {
inline constexpr std::uint32_t psf_dimension_max = core::psf_dimension_max;
inline constexpr std::size_t psf_encoded_bytes_max = core::psf_encoded_bytes_max;
struct IdentifiedPsf {
    image::Raster raster;
    core::ContentIdentity source;
};
// Raw stored 8/16-bit gray PNG coefficients. No alpha, color, orientation or profile transform.
[[nodiscard]] core::Result<image::Raster>
decode_psf_png(std::span<const std::uint8_t> bytes, core::Budget& budget,
               const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<IdentifiedPsf> load_psf_png(const std::string& path,
                                                       core::Budget& budget,
                                                       const core::Cancellation& cancellation = {});
} // namespace docenhance::io
