// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "jpeg_context.hpp"
#include "tiff_ifd.hpp"
#include "tiff_layout.hpp"

#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <docenhance_tiff.h>
#include <functional>
#include <span>
#include <tiffio.h>
namespace docenhance::io {
struct TiffContext {
    explicit TiffContext(core::Budget& budget, const core::Cancellation& control) noexcept
        : cancellation(control), jpeg(budget, control, unused_jump) {}
    TiffContext(const TiffContext&) = delete;
    TiffContext& operator=(const TiffContext&) = delete;
    TiffContext(TiffContext&&) = delete;
    TiffContext& operator=(TiffContext&&) = delete;
    ~TiffContext();
    std::reference_wrapper<const core::Cancellation> cancellation;
    std::span<const std::uint8_t> bytes;
    std::size_t position{};
    TiffIfd ifd;
    TiffLayout layout;
    TIFF* decoder = nullptr;
    bool failed = false;
    bool exhausted = false;
    bool cancelled = false;
    std::jmp_buf unused_jump{};
    JpegContext jpeg;
    [[nodiscard]] core::Error error() const;
    [[nodiscard]] bool good() const noexcept;
};
[[nodiscard]] bool open_tiff(TiffContext& state);
void bind_tiff_jpeg(TiffContext& state, DocEnhanceTiffJpegControl& installer);
[[nodiscard]] core::Result<image::TiffSource> tiff_description(const TiffIfd& ifd);
[[nodiscard]] core::Result<image::RasterMetadata>
tiff_metadata(const TiffIfd& ifd, const image::TiffSource& source, core::Budget& budget,
              image::ProfilePolicy policy, const core::Cancellation& cancellation);
[[nodiscard]] core::Result<void> tiff_pixels(TiffContext const& context,
                                             const image::TiffSource& source, image::Raster& raster,
                                             core::Budget& budget);
} // namespace docenhance::io
