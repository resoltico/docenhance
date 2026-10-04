// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "context.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/raster.hpp"

#include <array>
#include <cstddef>
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
core::Result<Profile> gray_profile(const Context& context, const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::verification)) {
        return core::cancelled();
    }
    const Curve curve = srgb_transfer(context);
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
Curve srgb_transfer(const Context& context) {
    constexpr int curve_type = 4;
    constexpr auto parameters =
        std::to_array<double>({2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045});
    return Curve{cmsBuildParametricToneCurve(context.get(), curve_type, parameters.data())};
}
Profile linear_rgb_profile(const Context& context) {
    Curve const curve{cmsBuildGamma(context.get(), 1.0)};
    if (!curve) {
        return {};
    }
    std::array<cmsToneCurve*, image::rgb_channels> curves{curve.get(), curve.get(), curve.get()};
    return Profile{cmsCreateRGBProfileTHR(context.get(), &d65, &primaries, curves.data())};
}
core::Result<core::Buffer> output_profile(const Context& context, core::Budget& budget, bool gray,
                                          const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::verification)) {
        return core::cancelled();
    }
    auto profile = gray ? gray_profile(context, cancellation)
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
