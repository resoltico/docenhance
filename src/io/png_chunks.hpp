// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"

#include <cstdint>
#include <span>
namespace docenhance::io {
struct PngChunk {
    std::uint32_t type{};
    std::span<const std::uint8_t> data;
};
[[nodiscard]] core::Result<PngChunk> take_png_chunk(std::span<const std::uint8_t>& bytes,
                                                    const core::Cancellation& cancellation);
} // namespace docenhance::io
