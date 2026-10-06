// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/source.hpp"
#include "tiff_ifd.hpp"

#include <cstddef>
#include <cstdint>
namespace docenhance::io {
struct TiffLayout {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t across{};
    std::uint32_t down{};
    unsigned planes{};
    std::size_t row_bytes{};
    std::size_t unit_bytes{};
    unsigned units{};
};
[[nodiscard]] core::Result<TiffLayout> tiff_layout(const TiffIfd& ifd,
                                                   const image::TiffSource& source,
                                                   const core::Cancellation& cancellation);
} // namespace docenhance::io
