// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "context.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
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
constexpr cmsCIExyY d65{.x = 0.3127, .y = 0.3290, .Y = 1};
constexpr cmsCIExyYTRIPLE primaries{
    .Red = {.x = 0.6400, .y = 0.3300, .Y = 1},
    .Green = {.x = 0.3000, .y = 0.6000, .Y = 1},
    .Blue = {.x = 0.1500, .y = 0.0600, .Y = 1},
};
core::Result<core::Buffer> serialize(const Context& context, void* const profile,
                                     core::Budget& budget) {
    cmsUInt32Number size = 0;
    if (profile == nullptr || cmsSaveProfileToMem(profile, nullptr, &size) == 0 ||
        size > image::profile_limit) {
        return std::unexpected(context.error("Cannot size the canonical output profile"));
    }
    auto storage = budget.allocate(size);
    if (!storage) {
        return std::unexpected(storage.error());
    }
    if (cmsSaveProfileToMem(profile, storage->bytes().data(), &size) == 0 ||
        size != storage->size()) {
        return std::unexpected(context.error("Cannot serialize the canonical output profile"));
    }
    return storage;
}
core::Result<Profile> gray_profile(const Context& context, core::Budget& budget) {
    constexpr std::uint32_t entries = 65530; // Largest table supported by the pinned Little CMS.
    constexpr double rounding_half = 0.5;
    auto table = image::Plane<std::uint16_t>::allocate(budget, entries, 1);
    if (!table) {
        return std::unexpected(table.error());
    }
    auto const values = table->view().row(0);
    for (std::uint32_t i = 0; i < entries; ++i) {
        const auto value = image::srgb_decode(static_cast<double>(i) / (entries - 1));
        if (!value) {
            return std::unexpected(value.error());
        }
        values.subspan(i, 1).front() =
            static_cast<std::uint16_t>(std::floor((*value * image::word_max) + rounding_half));
    }
    // ICC curveType stores uint16 samples. Little CMS serializes a floating curve through
    // a 4096-point approximation; supplying the explicit table retains all 65530 knots.
    Curve const curve{cmsBuildTabulatedToneCurve16(context.get(), entries, values.data())};
    if (!curve) {
        return std::unexpected(context.error("Cannot build the gray sRGB transfer curve"));
    }
    Profile profile{cmsCreateGrayProfileTHR(context.get(), cmsD50_xyY(), curve.get())};
    if (!profile) {
        return std::unexpected(context.error("Cannot build the gray output profile"));
    }
    return profile;
}
} // namespace
Profile linear_rgb_profile(const Context& context) {
    Curve const curve{cmsBuildGamma(context.get(), 1.0)};
    if (!curve) {
        return {};
    }
    std::array<cmsToneCurve*, image::rgb_channels> curves{curve.get(), curve.get(), curve.get()};
    return Profile{cmsCreateRGBProfileTHR(context.get(), &d65, &primaries, curves.data())};
}
core::Result<core::Buffer> output_profile(const Context& context, core::Budget& budget, bool gray) {
    auto profile = gray ? gray_profile(context, budget)
                        : core::Result<Profile>{Profile{cmsCreate_sRGBProfileTHR(context.get())}};
    if (!profile) {
        return std::unexpected(profile.error());
    }
    auto bytes = serialize(context, profile->get(), budget);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    constexpr std::size_t header_bytes = 128;
    if (bytes->size() < header_bytes) {
        return core::failure(core::ErrorCode::invariant, "Output ICC header is missing");
    }
    // Normalize the standard ICC date fields before recomputing the profile ID.
    constexpr auto date = std::to_array<unsigned>({2000, 1, 1, 0, 0, 0});
    constexpr std::size_t date_offset = 24;
    for (std::size_t i = 0; i < date.size(); ++i) {
        bytes->bytes().subspan(date_offset + (2 * i), 1).front() =
            static_cast<std::byte>(date.at(i) >> image::byte_bits);
        bytes->bytes().subspan(date_offset + (2 * i) + 1, 1).front() =
            static_cast<std::byte>(date.at(i) & image::byte_max);
    }
    Profile const fixed{cmsOpenProfileFromMemTHR(context.get(), bytes->bytes().data(),
                                                 static_cast<cmsUInt32Number>(bytes->size()))};
    if (!fixed || cmsMD5computeID(fixed.get()) == 0 || !context.good()) {
        return std::unexpected(context.error("Cannot finalize canonical output profile identity"));
    }
    return serialize(context, fixed.get(), budget);
}
} // namespace docenhance::color
