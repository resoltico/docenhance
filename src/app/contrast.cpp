// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/contrast.hpp"

#include <expected>
#include <optional>
#include <string>
namespace docenhance::app {
namespace {
core::Result<methods::Contrast> gamma(const contract::Invocation& i, double blend) {
    if (i.levels_low || i.levels_high) {
        return core::failure(core::ErrorCode::argument,
                             "Levels parameters require --contrast levels");
    }
    auto gamma = i.gamma ? contract::parse_finite(*i.gamma, methods::Gamma::minimum_exponent,
                                                  methods::Gamma::maximum_exponent)
                         : core::Result<double>{methods::gamma_default_exponent};
    if (!gamma) {
        return std::unexpected(gamma.error());
    }
    return methods::Gamma::create({.gamma = *gamma, .blend = blend})
        .transform([](auto method) -> methods::Contrast { return method; });
}
core::Result<methods::Contrast> levels(const contract::Invocation& i, double blend) {
    if (i.gamma) {
        return core::failure(core::ErrorCode::argument, "--gamma requires --contrast gamma");
    }
    auto low = i.levels_low ? contract::parse_finite(*i.levels_low, 0, methods::Levels::maximum_low)
                            : core::Result<double>{methods::levels_default_low};
    auto high = i.levels_high
                    ? contract::parse_finite(*i.levels_high, methods::Levels::minimum_high,
                                             methods::Levels::maximum_high)
                    : core::Result<double>{methods::levels_default_high};
    if (!low) {
        return std::unexpected(low.error());
    }
    if (!high) {
        return std::unexpected(high.error());
    }
    return methods::Levels::create({.low = *low, .high = *high, .blend = blend})
        .transform([](auto method) -> methods::Contrast { return method; });
}
} // namespace
core::Result<methods::Contrast> prepare_contrast(const contract::Invocation& i) {
    const bool parameters = i.contrast_blend || i.levels_low || i.levels_high || i.gamma;
    if (i.output_mode == "bw" && (i.contrast || parameters)) {
        return core::failure(core::ErrorCode::argument, "Contrast requires continuous output");
    }
    const auto mode = i.contrast.value_or("off");
    if (mode == "off") {
        if (parameters) {
            return core::failure(core::ErrorCode::argument,
                                 "Contrast parameters require their selected method");
        }
        return methods::ContrastOff{};
    }
    auto blend = i.contrast_blend ? contract::parse_finite(*i.contrast_blend, 0, 1)
                                  : core::Result<double>{methods::contrast_default_blend};
    if (!blend) {
        return std::unexpected(blend.error());
    }
    if (mode == "gamma") {
        return gamma(i, *blend);
    }
    if (mode != "levels") {
        return core::failure(core::ErrorCode::argument, "--contrast requires off, levels or gamma");
    }
    return levels(i, *blend);
}
} // namespace docenhance::app
