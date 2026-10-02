// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "png_context.hpp"

#include <cstdint>
#include <filesystem>

namespace docenhance::io {
inline constexpr const char* output_profile_name = "DocEnhance";
void install_png_writer(PngContext& context);
[[nodiscard]] core::Result<void> encode_png_rows(const BundleSlot& slot, image::RowSource& source,
                                                 core::Budget& budget,
                                                 const core::Cancellation& cancellation);
[[nodiscard]] core::Result<void> verify_png_rows(const std::filesystem::path& path,
                                                 image::RowSource& source, core::Budget& budget,
                                                 const core::Cancellation& cancellation,
                                                 const EntryLease* owner = nullptr);
// Binary output read back through the same row verifier: it carries no profile and no
// resolution, and the absence of both is part of what is compared.
[[nodiscard]] core::Result<void> verify_png_image(const std::filesystem::path& path,
                                                  image::PlaneView<const std::uint8_t> image,
                                                  core::Budget& budget,
                                                  const core::Cancellation& cancellation = {},
                                                  const EntryLease* owner = nullptr);
[[nodiscard]] bool output_inventory(PngContext& context, const core::Cancellation& cancellation);
} // namespace docenhance::io
