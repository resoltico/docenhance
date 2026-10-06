// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"

#include <cstdint>
namespace docenhance::image {
RasterShape decoded_tiff_shape(const TiffSource& source) noexcept {
    SampleModel model = SampleModel::gray;
    if (source.photometric >= 2) {
        model = source.alpha != 0 ? SampleModel::rgba : SampleModel::rgb;
    } else if (source.alpha != 0) {
        model = SampleModel::gray_alpha;
    }
    return {
        .width = source.width,
        .height = source.height,
        .model = model,
        .depth = source.depth == word_bits || source.photometric == tiff_palette
                     ? SampleDepth::word()
                     : SampleDepth::byte(),
    };
}
namespace {
bool valid_predictor(const TiffSource& source) noexcept {
    return source.predictor == 1 ||
           (source.predictor == 2 && source.depth != 1 &&
            (source.compression == tiff_lzw || source.compression == tiff_deflate ||
             source.compression == tiff_adobe_deflate));
}
} // namespace
bool valid_tiff_source(const TiffSource& source) noexcept {
    const bool gray = source.photometric <= 1;
    const bool rgb = source.photometric == 2;
    const bool palette = source.photometric == tiff_palette;
    const bool ycc = source.photometric == tiff_ycbcr;
    const unsigned base = (rgb || ycc) ? tiff_color_samples : 1;
    const bool depth = source.depth == byte_bits || source.depth == word_bits ||
                       (gray && source.depth == 1 && source.alpha == 0);
    const bool samples = source.samples == base + (source.alpha != 0 ? 1U : 0U);
    const bool compression = source.compression == 1 || source.compression == tiff_lzw ||
                             source.compression == tiff_deflate ||
                             source.compression == tiff_adobe_deflate ||
                             source.compression == tiff_packbits;
    const bool fax =
        (source.compression == tiff_ccitt_group3 || source.compression == tiff_ccitt_group4) &&
        gray && source.depth == 1 && source.alpha == 0;
    const bool jpeg = source.compression == tiff_jpeg && !palette && source.depth == byte_bits &&
                      source.alpha == 0 && (gray || rgb || ycc);
    return source.width != 0 && source.height != 0 &&
           std::uint64_t{source.width} * source.height <= source_pixels_max &&
           (gray || rgb || palette || ycc) && depth && samples && source.alpha <= 2 &&
           (!palette || (source.depth == byte_bits && source.alpha == 0)) &&
           (!ycc || (jpeg && source.planar == 1)) && (source.planar == 1 || source.planar == 2) &&
           (compression || fax || jpeg) && valid_predictor(source) &&
           (!source.resolution || (source.resolution->x != 0 && source.resolution->y != 0));
}
} // namespace docenhance::image
