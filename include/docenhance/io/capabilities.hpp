// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <span>
#include <string>
#include <string_view>
namespace docenhance::io {
// Linked codec libraries do not imply implemented, contract-compliant decoders.
[[nodiscard]] std::span<const std::string_view> supported_input_formats() noexcept;
[[nodiscard]] core::Result<image::Plane<std::uint8_t>> load_grayscale_png(const std::string& input,
                                                                          core::Budget& budget);
[[nodiscard]] core::Result<std::string>
publish_grayscale_png(const std::string& output_directory,
                      image::PlaneView<const std::uint8_t> image);
} // namespace docenhance::io
