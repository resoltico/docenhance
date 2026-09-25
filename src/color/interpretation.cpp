// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "context.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/raster.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <lcms2.h>
#include <span>

namespace docenhance::color {
namespace {
constexpr auto srgb_chroma =
    std::to_array<std::uint32_t>({31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000});
constexpr std::uint32_t srgb_gamma = 45455;
constexpr std::uint32_t png_scale_integer = 100000;
constexpr double png_scale = png_scale_integer;
constexpr auto srgb_cicp = std::to_array<std::uint8_t>({1, 13, 0, 1});
cmsCIExyY xy(const std::array<std::uint32_t, image::chromaticity_fields>& values,
             std::size_t index) {
    return {.x = values.at(index) / png_scale, .y = values.at(index + 1) / png_scale, .Y = 1};
}
bool valid_chroma(const std::array<std::uint32_t, image::chromaticity_fields>& values) {
    for (std::size_t i = 0; i < values.size(); i += 2) {
        if (values.at(i + 1) == 0 || values.at(i) > png_scale || values.at(i + 1) > png_scale ||
            std::uint64_t{values.at(i)} + values.at(i + 1) > png_scale_integer) {
            return false;
        }
    }
    constexpr std::size_t red = 2;
    constexpr std::size_t green = 4;
    constexpr std::size_t blue = 6;
    const auto r = xy(values, red);
    const auto g = xy(values, green);
    const auto b = xy(values, blue);
    const double area = ((r.x - b.x) * (g.y - b.y)) - ((g.x - b.x) * (r.y - b.y));
    constexpr double min_area = 1e-8;
    if (std::abs(area) <= min_area) {
        return false;
    }
    const auto w = xy(values, 0);
    const double red_weight = (((w.x - b.x) * (g.y - b.y)) - ((g.x - b.x) * (w.y - b.y))) / area;
    const double green_weight = (((r.x - b.x) * (w.y - b.y)) - ((w.x - b.x) * (r.y - b.y))) / area;
    return red_weight > 0 && green_weight > 0 && red_weight + green_weight < 1;
}
core::Result<Profile> embedded_profile(const Context& context, const image::Raster& source) {
    auto const bytes = source.metadata.icc.bytes();
    constexpr std::size_t header_size = 128;
    std::uint32_t declared = 0;
    if (bytes.size() < header_size || bytes.size() > image::profile_limit) {
        return core::failure(core::ErrorCode::input, "ICC profile extent is invalid");
    }
    for (const auto b : bytes.first(4)) {
        declared = (declared << image::byte_bits) | std::to_integer<unsigned>(b);
    }
    if (declared != bytes.size()) {
        return core::failure(core::ErrorCode::input, "ICC declared size does not match its bytes");
    }
    Profile profile{cmsOpenProfileFromMemTHR(context.get(), bytes.data(),
                                             static_cast<cmsUInt32Number>(bytes.size()))};
    if (!profile || !context.good()) {
        return std::unexpected(context.error("Invalid embedded ICC profile"));
    }
    // Native C enums carry arbitrary profile bytes. Preserve their unsigned representation;
    // implicit promotion to int changes high-bit malformed signatures before rejection.
    const auto wanted = static_cast<std::uint32_t>(
        image::is_color(source.shape.model) ? cmsSigRgbData : cmsSigGrayData);
    const auto actual = static_cast<std::uint32_t>(cmsGetColorSpace(profile.get()));
    const auto kind = static_cast<std::uint32_t>(cmsGetDeviceClass(profile.get()));
    if (actual != wanted || (kind != static_cast<std::uint32_t>(cmsSigInputClass) &&
                             kind != static_cast<std::uint32_t>(cmsSigDisplayClass) &&
                             kind != static_cast<std::uint32_t>(cmsSigOutputClass) &&
                             kind != static_cast<std::uint32_t>(cmsSigColorSpaceClass))) {
        return core::failure(core::ErrorCode::input,
                             "ICC profile is incompatible with decoded PNG samples");
    }
    return profile;
}
Curve transfer_curve(const Context& context, const image::PngMetadata& metadata) {
    if (metadata.gamma) {
        return Curve{cmsBuildGamma(context.get(), png_scale / *metadata.gamma)};
    }
    constexpr int srgb_curve_type = 4;
    constexpr auto parameters =
        std::to_array<double>({2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045});
    return Curve{cmsBuildParametricToneCurve(context.get(), srgb_curve_type, parameters.data())};
}
} // namespace
core::Result<void> validate_declarations(const image::PngMetadata& metadata) {
    constexpr unsigned last_intent = 3;
    if (metadata.srgb && (*metadata.srgb > last_intent || !metadata.icc.empty())) {
        return core::failure(core::ErrorCode::input, "Invalid or conflicting PNG sRGB declaration");
    }
    if (metadata.gamma && *metadata.gamma == 0) {
        return core::failure(core::ErrorCode::input, "PNG gamma must be positive");
    }
    if (metadata.chromaticities && !valid_chroma(*metadata.chromaticities)) {
        return core::failure(core::ErrorCode::input,
                             "PNG chromaticities are invalid or degenerate");
    }
    if (metadata.srgb && ((metadata.gamma && *metadata.gamma != srgb_gamma) ||
                          (metadata.chromaticities && *metadata.chromaticities != srgb_chroma))) {
        return core::failure(core::ErrorCode::input, "PNG color declarations conflict with sRGB");
    }
    if (metadata.cicp && *metadata.cicp != srgb_cicp) {
        return core::failure(
            core::ErrorCode::input,
            "Unsupported PNG cICP interpretation; only full-range sRGB is supported");
    }
    return {};
}
core::Result<Profile> source_profile(const Context& context, const image::Raster& source) {
    if (!source.metadata.icc.empty()) {
        return embedded_profile(context, source);
    }
    const auto chroma = source.metadata.chromaticities.value_or(srgb_chroma);
    const auto white = xy(chroma, 0);
    Curve const curve = transfer_curve(context, source.metadata);
    if (!curve) {
        return std::unexpected(context.error("Cannot construct the PNG transfer curve"));
    }
    if (!image::is_color(source.shape.model)) {
        return Profile{cmsCreateGrayProfileTHR(context.get(), &white, curve.get())};
    }
    constexpr std::size_t red = 2;
    constexpr std::size_t green = 4;
    constexpr std::size_t blue = 6;
    const cmsCIExyYTRIPLE primaries{
        .Red = xy(chroma, red),
        .Green = xy(chroma, green),
        .Blue = xy(chroma, blue),
    };
    std::array<cmsToneCurve*, image::rgb_channels> curves{curve.get(), curve.get(), curve.get()};
    return Profile{cmsCreateRGBProfileTHR(context.get(), &white, &primaries, curves.data())};
}
} // namespace docenhance::color
