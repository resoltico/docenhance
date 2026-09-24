// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "png_context.hpp"

#include <filesystem>

namespace docenhance::io {
inline constexpr const char* output_profile_name = "DocEnhance";
void install_png_writer(PngContext& context);
[[nodiscard]] core::Result<void> encode_png_rows(const std::filesystem::path& path,
                                                 image::RowSource& source, core::Budget& budget,
                                                 const core::Cancellation& cancellation);
[[nodiscard]] core::Result<void> verify_png_rows(const std::filesystem::path& path,
                                                 image::RowSource& source, core::Budget& budget,
                                                 const core::Cancellation& cancellation);
[[nodiscard]] bool output_inventory(PngContext& context, const core::Cancellation& cancellation);
} // namespace docenhance::io
