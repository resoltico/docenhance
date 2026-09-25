// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "illumination.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/contract/utf8.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/illumination.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace docenhance::app {
namespace {
core::Result<std::optional<std::uint32_t>> cell_value(const std::optional<std::string>& raw) {
    if (!raw || *raw == "auto") {
        return std::nullopt;
    }
    if (raw->empty() || !std::ranges::all_of(*raw, [](char c) { return c >= '0' && c <= '9'; })) {
        return core::failure(core::ErrorCode::argument,
                             "--background-cell requires auto or digits");
    }
    std::uint32_t value{};
    const auto result =
        std::from_chars(std::to_address(raw->begin()), std::to_address(raw->end()), value);
    if (result.ec != std::errc{} || result.ptr != std::to_address(raw->end())) {
        return core::failure(core::ErrorCode::argument,
                             "--background-cell exceeds its integer range");
    }
    return value;
}
core::Result<double> numeric(const std::optional<std::string>& raw, double fallback) {
    // The typed factory owns option-specific domains; parsing owns finite decimal spelling.
    constexpr double parameter_limit = 20;
    return raw ? contract::parse_finite(*raw, 0, parameter_limit) : core::Result<double>{fallback};
}
core::Result<methods::Surface> surface(const contract::Invocation& v) {
    const methods::SurfaceParameters defaults;
    const auto strength = numeric(v.background_strength, defaults.strength);
    const auto gain = numeric(v.background_max_gain, defaults.max_gain);
    const auto quantile = numeric(v.background_quantile, defaults.quantile);
    const auto smooth = numeric(v.background_smooth, defaults.smooth);
    const auto cell = cell_value(v.background_cell);
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
        const auto parsed = numeric(v.background_target, 0);
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
} // namespace
core::Result<methods::Illumination> prepare_illumination(const contract::Invocation& v) {
    const bool parameters = v.background_strength || v.background_max_gain || v.background_target ||
                            v.background_cell || v.background_quantile || v.background_smooth;
    if (v.output_mode == "bw" && (v.illumination || parameters || v.protect_mask)) {
        return core::failure(core::ErrorCode::argument,
                             "Illumination and protection require continuous-tone output");
    }
    if (v.protect_mask && (v.protect_mask->empty() || v.protect_mask->contains('\0') ||
                           !contract::valid_utf8(*v.protect_mask))) {
        return core::failure(core::ErrorCode::argument,
                             "--protect-mask needs a nonempty UTF-8 path without NUL");
    }
    const auto mode = v.illumination.value_or("off");
    if (mode == "off") {
        if (parameters) {
            return core::failure(core::ErrorCode::argument,
                                 "Background parameters require surface or auto illumination");
        }
        return methods::IlluminationOff{};
    }
    if (mode != "surface" && mode != "auto") {
        return core::failure(core::ErrorCode::argument,
                             "--illumination requires off, surface or auto");
    }
    return surface(v).transform([](auto value) -> methods::Illumination { return value; });
}
} // namespace docenhance::app
