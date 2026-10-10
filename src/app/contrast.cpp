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
#include <string_view>
namespace docenhance::app {
namespace {
core::Result<methods::Contrast> gamma(const contract::Invocation& i, double blend) {
    if (i.levels_low || i.levels_high) {
        return core::failure(core::ErrorCode::argument,
                             "Levels parameters require --contrast levels");
    }
    auto gamma = i.gamma ? contract::parse_decimal_option("--gamma", *i.gamma,
                                                          methods::Gamma::minimum_exponent,
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
    auto low = i.levels_low ? contract::parse_decimal_option("--levels-low", *i.levels_low, 0,
                                                             methods::Levels::maximum_low)
                            : core::Result<double>{methods::levels_default_low};
    auto high = i.levels_high ? contract::parse_decimal_option("--levels-high", *i.levels_high,
                                                               methods::Levels::minimum_high,
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
core::Result<methods::Contrast> clahe(const contract::Invocation& i, double blend) {
    if (i.gamma || i.levels_low || i.levels_high) {
        return core::failure(core::ErrorCode::argument, "CLAHE rejects other contrast parameters");
    }
    methods::ClaheParameters p;
    p.blend = blend;
    if (i.clahe_grid) {
        const auto& grid = *i.clahe_grid;
        const auto split = grid.find('x');
        if (split == std::string::npos) {
            return std::unexpected(contract::option_error(
                {.name = "--clahe-grid", .value = grid, .reason = "Expected CxR"}));
        }
        if (split > 2 || grid.size() - split - 1 > 2) {
            return std::unexpected(contract::option_error({
                .name = "--clahe-grid",
                .value = grid,
                .reason = "Expected CxR with one or two decimal digits per component",
            }));
        }
        auto columns =
            contract::parse_integer_option("--clahe-grid", std::string_view(grid).substr(0, split),
                                           2, methods::clahe_maximum_grid);
        auto rows =
            contract::parse_integer_option("--clahe-grid", std::string_view(grid).substr(split + 1),
                                           2, methods::clahe_maximum_grid);
        if (!columns) {
            return std::unexpected(contract::option_error({
                .name = "--clahe-grid",
                .value = grid,
                .reason = "Expected CxR with each integer in the permitted domain",
            }));
        }
        if (!rows) {
            return std::unexpected(contract::option_error({
                .name = "--clahe-grid",
                .value = grid,
                .reason = "Expected CxR with each integer in the permitted domain",
            }));
        }
        p.grid_columns = *columns;
        p.grid_rows = *rows;
    }
    if (i.clahe_clip) {
        auto clip = contract::parse_decimal_option("--clahe-clip", *i.clahe_clip, 1,
                                                   methods::clahe_maximum_clip);
        if (!clip) {
            return std::unexpected(clip.error());
        }
        p.clip = *clip;
    }
    return methods::Clahe::create(p).transform(
        [](auto method) -> methods::Contrast { return method; });
}
} // namespace
core::Result<methods::Contrast> prepare_contrast(const contract::Invocation& i) {
    const bool parameters = i.contrast_blend || i.levels_low || i.levels_high || i.gamma ||
                            i.clahe_grid || i.clahe_clip;
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
    auto blend = i.contrast_blend
                     ? contract::parse_decimal_option("--contrast-blend", *i.contrast_blend, 0, 1)
                     : core::Result<double>{methods::contrast_default_blend};
    if (!blend) {
        return std::unexpected(blend.error());
    }
    if (mode == "clahe") {
        return clahe(i, *blend);
    }
    if (i.clahe_grid || i.clahe_clip) {
        return core::failure(core::ErrorCode::argument,
                             "CLAHE parameters require --contrast clahe");
    }
    if (mode == "gamma") {
        return gamma(i, *blend);
    }
    if (mode != "levels") {
        return core::failure(core::ErrorCode::argument,
                             "--contrast requires off, levels, gamma or clahe");
    }
    return levels(i, *blend);
}
} // namespace docenhance::app
