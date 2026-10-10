// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "illumination.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/methods/illumination.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace docenhance::app {
namespace {
core::Result<std::optional<std::uint32_t>> integer_value(const std::optional<std::string>& raw,
                                                         std::string_view name, std::uint32_t low,
                                                         std::uint32_t high) {
    if (!raw || *raw == "auto") {
        return std::nullopt;
    }
    return contract::parse_integer_option(name, *raw, low, high).transform([](auto value) {
        return std::optional<std::uint32_t>{value};
    });
}
core::Result<double> numeric(const std::optional<std::string>& raw, std::string_view name,
                             double fallback, double low, double high) {
    return raw ? contract::parse_decimal_option(name, *raw, low, high)
               : core::Result<double>{fallback};
}
core::Result<methods::Surface> surface(const contract::Invocation& v) {
    const methods::SurfaceParameters defaults;
    const auto strength =
        numeric(v.background_strength, "--background-strength", defaults.strength, 0, 1);
    const auto gain = numeric(v.background_max_gain, "--background-max-gain", defaults.max_gain, 1,
                              methods::illumination_gain_limit);
    const auto quantile = numeric(v.background_quantile, "--background-quantile", defaults.quantile,
                                  methods::surface_min_quantile, methods::surface_max_quantile);
    const auto smooth = numeric(v.background_smooth, "--background-smooth", defaults.smooth,
                                methods::surface_min_smooth, methods::surface_max_smooth);
    const auto cell = integer_value(v.background_cell, "--background-cell",
                                    methods::Surface::min_cell, methods::Surface::max_cell);
    if (!strength) {
        return std::unexpected(strength.error());
    }
    if (!gain) {
        return std::unexpected(gain.error());
    }
    if (!quantile) {
        return std::unexpected(quantile.error());
    }
    if (!smooth) {
        return std::unexpected(smooth.error());
    }
    if (!cell) {
        return std::unexpected(cell.error());
    }
    std::optional<double> target;
    if (v.background_target && *v.background_target != "source") {
        const auto parsed = numeric(v.background_target, "--background-target", 0,
                                    methods::illumination_min_target, 1);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        target = *parsed;
    }
    return methods::Surface::create({
        .mode = v.illumination == "auto" ? methods::SurfaceMode::automatic
                                         : methods::SurfaceMode::explicit_surface,
        .strength = *strength,
        .max_gain = *gain,
        .target = target,
        .cell = *cell,
        .quantile = *quantile,
        .smooth = *smooth,
    });
}
core::Result<methods::Morphology> morphology(const contract::Invocation& v) {
    const methods::MorphologyParameters defaults;
    const auto strength =
        numeric(v.background_strength, "--background-strength", defaults.strength, 0, 1);
    const auto gain = numeric(v.background_max_gain, "--background-max-gain", defaults.max_gain, 1,
                              methods::illumination_gain_limit);
    const auto radius =
        integer_value(v.background_radius, "--background-radius", methods::Morphology::min_radius,
                      methods::Morphology::max_radius);
    if (!strength) {
        return std::unexpected(strength.error());
    }
    if (!gain) {
        return std::unexpected(gain.error());
    }
    if (!radius) {
        return std::unexpected(radius.error());
    }
    std::optional<double> target;
    if (v.background_target && *v.background_target != "source") {
        const auto parsed = numeric(v.background_target, "--background-target", 0,
                                    methods::illumination_min_target, 1);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        target = *parsed;
    }
    return methods::Morphology::create(
        {.strength = *strength, .max_gain = *gain, .target = target, .radius = *radius});
}
} // namespace
core::Result<methods::Illumination> prepare_illumination(const contract::Invocation& v) {
    const bool parameters = v.background_strength || v.background_max_gain || v.background_target ||
                            v.background_cell || v.background_quantile || v.background_smooth ||
                            v.background_radius;
    if (v.output_mode == "bw" && (v.illumination || parameters || v.protect_mask)) {
        return core::failure(core::ErrorCode::argument,
                             "Illumination and protection require continuous-tone output");
    }
    if (v.protect_mask && (v.protect_mask->empty() || v.protect_mask->contains('\0') ||
                           !core::valid_utf8(*v.protect_mask))) {
        return core::failure(core::ErrorCode::argument,
                             "--protect-mask needs a nonempty UTF-8 path without NUL");
    }
    const auto mode = v.illumination.value_or("off");
    if (mode == "off") {
        if (parameters) {
            return core::failure(
                core::ErrorCode::argument,
                "Background parameters require surface, auto or morph illumination");
        }
        return methods::IlluminationOff{};
    }
    if (mode == "morph") {
        if (v.background_cell || v.background_quantile || v.background_smooth) {
            return core::failure(core::ErrorCode::argument,
                                 "Cell, quantile and smooth options require I01 surface or auto");
        }
        return morphology(v).transform([](auto value) -> methods::Illumination { return value; });
    }
    if (v.background_radius) {
        return core::failure(core::ErrorCode::argument,
                             "--background-radius requires morph illumination");
    }
    if (mode != "surface" && mode != "auto") {
        return core::failure(core::ErrorCode::argument,
                             "--illumination requires off, surface, auto or morph");
    }
    return surface(v).transform([](auto value) -> methods::Illumination { return value; });
}
} // namespace docenhance::app
