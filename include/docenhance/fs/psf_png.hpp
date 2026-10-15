// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/codec/psf_png.hpp"
#include "docenhance/core/identity.hpp"

#include <string>
namespace docenhance::fs {
struct IdentifiedPsf {
    image::Raster raster;
    core::ContentIdentity source;
};
[[nodiscard]] core::Result<IdentifiedPsf> load_psf_png(const std::string& path,
                                                       core::Budget& budget,
                                                       const core::Cancellation& cancellation = {});
} // namespace docenhance::fs
