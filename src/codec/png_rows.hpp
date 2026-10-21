// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/codec/png_artifact.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "png_context.hpp"

#include <cstdint>

namespace docenhance::codec {
inline constexpr const char* output_profile_name = "DocEnhance";
void install_png_writer(PngContext& context);
void install_canonical_gray1_tail(PngContext& context, std::uint32_t width);
[[nodiscard]] bool canonical_png_chunks(std::span<const std::uint8_t> bytes,
                                        const core::Cancellation& cancellation, bool& stopped);
[[nodiscard]] core::Result<PngArtifact>
describe_canonical_png(std::span<const std::byte> bytes, const PngImage* intended,
                       core::Budget& budget, const core::Cancellation& cancellation);
[[nodiscard]] core::Result<PngArtifact> read_canonical_png(std::span<const std::byte> bytes,
                                                           const PngImage* intended,
                                                           core::Budget& budget,
                                                           const core::Cancellation& cancellation);
} // namespace docenhance::codec
