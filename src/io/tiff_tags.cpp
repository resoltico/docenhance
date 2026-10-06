// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "tiff_context.hpp"
#include "tiff_ifd.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <tiff.h>
#include <tiffio.h>
#include <utility>
namespace docenhance::io {
namespace {
core::Result<image::TiffSource> invalid() {
    return core::failure(core::ErrorCode::input,
                         "TIFF sample, compression or metadata domain is unsupported");
}
std::optional<unsigned> small(const TiffIfd& ifd, unsigned tag, unsigned fallback) noexcept {
    const auto value = ifd.integer(tag, fallback);
    if (!value || *value > UINT32_MAX) {
        return std::nullopt;
    }
    return static_cast<unsigned>(*value);
}
struct UniformSamples {
    unsigned count{};
    unsigned value{};
};
bool uniform(const TiffIfd& ifd, unsigned tag, UniformSamples samples) noexcept {
    const auto value = ifd.field(tag);
    if (!value) {
        return samples.value == 1;
    }
    if (value->type != TIFF_SHORT || (value->count != 1 && value->count != samples.count)) {
        return false;
    }
    for (std::size_t i = 0; i < value->bytes.size(); i += 2) {
        if (ifd.number(value->bytes.subspan(i, 2)) != samples.value) {
            return false;
        }
    }
    return true;
}
std::optional<double> rational(const TiffIfd& ifd, unsigned tag) noexcept {
    const auto value = ifd.field(tag);
    if (!value || value->type != TIFF_RATIONAL || value->count != 1) {
        return std::nullopt;
    }
    const auto denominator =
        ifd.number(value->bytes.subspan(sizeof(std::uint32_t), sizeof(std::uint32_t)));
    if (denominator == 0) {
        return std::nullopt;
    }
    return static_cast<double>(ifd.number(value->bytes.first(sizeof(std::uint32_t)))) /
           static_cast<double>(denominator);
}
core::Result<std::optional<image::Resolution>> resolution(const TiffIfd& ifd) {
    const auto x = rational(ifd, TIFFTAG_XRESOLUTION);
    const auto y = rational(ifd, TIFFTAG_YRESOLUTION);
    const auto unit = small(ifd, TIFFTAG_RESOLUTIONUNIT, 2);
    if (!unit || *unit == 0 || *unit > RESUNIT_CENTIMETER ||
        ((ifd.field(TIFFTAG_XRESOLUTION) || ifd.field(TIFFTAG_YRESOLUTION)) &&
         (!x || !y || *x <= 0 || *y <= 0))) {
        return core::failure(core::ErrorCode::input, "TIFF resolution is malformed");
    }
    if (!x || !y || *unit == 1) {
        return std::nullopt;
    }
    constexpr double inches_per_metre = 10000.0 / 254.0;
    constexpr double centimetres_per_metre = 100;
    const auto factor = *unit == RESUNIT_INCH ? inches_per_metre : centimetres_per_metre;
    const auto xp = std::round(*x * factor);
    const auto yp = std::round(*y * factor);
    if (xp < 1 || yp < 1 || xp > UINT32_MAX || yp > UINT32_MAX) {
        return core::failure(core::ErrorCode::input, "TIFF resolution is out of range");
    }
    return image::Resolution{
        .x = static_cast<std::uint32_t>(xp),
        .y = static_cast<std::uint32_t>(yp),
    };
}
core::Result<unsigned> alpha_channel(const TiffIfd& ifd) {
    const auto extra = ifd.field(TIFFTAG_EXTRASAMPLES);
    if (!extra) {
        return 0U;
    }
    if (extra->type != TIFF_SHORT || extra->count != 1) {
        return core::failure(core::ErrorCode::input, "TIFF extra channels are unsupported");
    }
    const auto alpha = static_cast<unsigned>(ifd.number(extra->bytes));
    if (alpha != EXTRASAMPLE_ASSOCALPHA && alpha != EXTRASAMPLE_UNASSALPHA) {
        return core::failure(core::ErrorCode::input,
                             "TIFF extra channel is not associated or unassociated alpha");
    }
    return alpha;
}
core::Result<core::Buffer> profile_bytes(const TiffIfd& ifd, core::Budget& budget,
                                         image::ProfilePolicy policy,
                                         const core::Cancellation& cancellation) {
    const auto icc = ifd.field(TIFFTAG_ICCPROFILE);
    if (!icc) {
        return core::Buffer{};
    }
    if (icc->type != TIFF_UNDEFINED || icc->bytes.empty()) {
        return core::failure(core::ErrorCode::input, "TIFF ICC field is malformed");
    }
    if (icc->bytes.size() > image::profile_limit) {
        return core::failure(core::ErrorCode::resource, "TIFF ICC exceeds 4 MiB");
    }
    if (policy == image::ProfilePolicy::srgb) {
        return core::Buffer{};
    }
    auto storage = budget.allocate(icc->bytes.size());
    if (!storage) {
        return std::unexpected(storage.error());
    }
    constexpr std::size_t transfer = std::size_t{64} * 1024;
    for (std::size_t position = 0; position < icc->bytes.size(); position += transfer) {
        if (cancellation.requested(core::Checkpoint::decode)) {
            return std::unexpected(core::cancelled().error());
        }
        const auto part = std::as_bytes(icc->bytes)
                              .subspan(position, std::min(transfer, icc->bytes.size() - position));
        std::ranges::copy(part, storage->bytes().subspan(position).begin());
    }
    return std::move(*storage);
}
} // namespace
core::Result<image::TiffSource> tiff_description(const TiffIfd& ifd) {
    const auto width = small(ifd, TIFFTAG_IMAGEWIDTH, 0);
    const auto height = small(ifd, TIFFTAG_IMAGELENGTH, 0);
    const auto samples = small(ifd, TIFFTAG_SAMPLESPERPIXEL, 1);
    const auto photo = small(ifd, TIFFTAG_PHOTOMETRIC, UINT32_MAX);
    const auto compression = small(ifd, TIFFTAG_COMPRESSION, 1);
    const auto planar = small(ifd, TIFFTAG_PLANARCONFIG, 1);
    const auto predictor = small(ifd, TIFFTAG_PREDICTOR, 1);
    const auto orientation = small(ifd, TIFFTAG_ORIENTATION, 1);
    const auto admitted_orientation =
        orientation ? image::Orientation::from_code(*orientation) : std::nullopt;
    const auto fill = small(ifd, TIFFTAG_FILLORDER, 1);
    const auto bits = ifd.field(TIFFTAG_BITSPERSAMPLE);
    const auto depth = bits && bits->type == TIFF_SHORT && bits->bytes.size() >= 2
                           ? static_cast<unsigned>(ifd.number(bits->bytes.first(2)))
                           : 1U;
    if (!width || !height || !samples || !photo || !compression || !planar || !predictor ||
        !admitted_orientation || !fill || (*fill != 1 && *fill != 2) ||
        !uniform(ifd, TIFFTAG_BITSPERSAMPLE, {.count = *samples, .value = depth}) ||
        !uniform(ifd, TIFFTAG_SAMPLEFORMAT, {.count = *samples, .value = 1})) {
        return invalid();
    }
    const auto alpha = alpha_channel(ifd);
    if (!alpha) {
        return std::unexpected(alpha.error());
    }
    if (ifd.field(TIFFTAG_TRANSFERFUNCTION) || ifd.field(TIFFTAG_WHITEPOINT) ||
        ifd.field(TIFFTAG_PRIMARYCHROMATICITIES)) {
        return core::failure(core::ErrorCode::input,
                             "TIFF transfer/colorimetry tags are not admitted; use ICC");
    }
    if (const auto page = ifd.field(TIFFTAG_PAGENUMBER);
        page &&
        (page->type != TIFF_SHORT || page->count != 2 || ifd.number(page->bytes.first(2)) != 0 ||
         ifd.number(page->bytes.subspan(2, 2)) > 1)) {
        return core::failure(core::ErrorCode::input,
                             "Multipage TIFF declarations are not supported");
    }
    auto density = resolution(ifd);
    if (!density) {
        return std::unexpected(density.error());
    }
    const image::TiffSource source{
        .width = *width,
        .height = *height,
        .depth = depth,
        .samples = *samples,
        .photometric = *photo,
        .compression = *compression,
        .planar = *planar,
        .predictor = *predictor,
        .alpha = *alpha,
        .big = ifd.big,
        .tiled = ifd.field(TIFFTAG_TILEWIDTH).has_value(),
        .orientation = *admitted_orientation,
        .resolution = *density,
    };
    if (std::uint64_t{source.width} * source.height > image::source_pixels_max) {
        return core::failure(core::ErrorCode::resource, "TIFF pixel limit exceeded");
    }
    if (!image::valid_tiff_source(source)) {
        return invalid();
    }
    return source;
}
core::Result<image::RasterMetadata> tiff_metadata(const TiffIfd& ifd,
                                                  const image::TiffSource& source,
                                                  core::Budget& budget, image::ProfilePolicy policy,
                                                  const core::Cancellation& cancellation) {
    image::RasterMetadata metadata{
        .icc = {},
        .orientation = source.orientation,
        .resolution = source.resolution,
        .declarations =
            image::TiffDeclarations{
                .associated_alpha = source.alpha == 1,
                .miniswhite = source.photometric == 0,
            },
    };
    auto profile = profile_bytes(ifd, budget, policy, cancellation);
    if (!profile) {
        return std::unexpected(profile.error());
    }
    metadata.icc = std::move(*profile);
    return metadata;
}
} // namespace docenhance::io
