// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <cstdint>
#include <string>

namespace docenhance::io {
// UTF-8 paths. Stored grayscale samples are expanded to 8 bits without gamma/color transforms.
// Images, libpng and zlib allocations share the supplied byte budget. Metadata and OS resources
// have separate fixed limits; this is not a process-RSS guarantee.
[[nodiscard]] core::Result<image::Plane<std::uint8_t>> load_grayscale_png(const std::string& input,
                                                                          core::Budget& budget);

// Publish result.png in a new directory. A native no-replace rename is the commit point; no
// fallback may overwrite an existing file, directory or symlink. The parent must be trusted
// against hostile ancestor replacement. Atomic visibility does not promise crash durability.
[[nodiscard]] core::Result<std::string>
publish_grayscale_png(const std::string& output_directory,
                      image::PlaneView<const std::uint8_t> image, core::Budget& budget);
} // namespace docenhance::io
