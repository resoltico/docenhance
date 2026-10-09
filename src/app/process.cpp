// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/process.hpp"

#include "contrast.hpp"
#include "denoising.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/binarization.hpp"
#include "illumination.hpp"
#include "restoration.hpp"
#include "sharpening.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace docenhance::app {
namespace {
core::Result<std::uint32_t> window_value(const std::optional<std::string>& value) {
    if (!value) {
        return methods::Sauvola::default_window;
    }
    std::uint32_t parsed = 0;
    if (value->empty() ||
        !std::ranges::all_of(*value, [](char byte) { return byte >= '0' && byte <= '9'; })) {
        return core::failure(core::ErrorCode::argument, "--sauvola-window requires decimal digits");
    }
    const auto conversion =
        std::from_chars(std::to_address(value->begin()), std::to_address(value->end()), parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != std::to_address(value->end())) {
        return core::failure(core::ErrorCode::argument,
                             "--sauvola-window is outside its integer range");
    }
    return parsed;
}
core::Result<double> value_or(const std::optional<std::string>& value, double fallback,
                              double low) {
    return value ? contract::parse_finite(*value, low, 1.0) : core::Result<double>{fallback};
}
core::Result<methods::Binarization> prepare_method(const contract::Invocation& invocation) {
    if (invocation.binarize == methods::Otsu::descriptor().selector) {
        if (invocation.fixed_threshold || invocation.sauvola_window || invocation.sauvola_k ||
            invocation.sauvola_r) {
            return core::failure(core::ErrorCode::argument,
                                 "Otsu accepts no method-specific options");
        }
        return methods::Binarization{methods::Otsu::create()};
    }
    if (invocation.binarize == methods::FixedThreshold::descriptor().selector) {
        if (invocation.sauvola_window || invocation.sauvola_k || invocation.sauvola_r) {
            return core::failure(core::ErrorCode::argument,
                                 "Sauvola options require --binarize sauvola");
        }
        const auto threshold =
            value_or(invocation.fixed_threshold, methods::FixedThreshold::default_threshold, 0.0);
        if (!threshold) {
            return std::unexpected(threshold.error());
        }
        return methods::FixedThreshold::create(*threshold)
            .transform([](auto value) -> methods::Binarization { return value; });
    }
    if (invocation.binarize.value_or("sauvola") != methods::Sauvola::descriptor().selector) {
        return core::failure(core::ErrorCode::argument,
                             "--binarize requires otsu, fixed or sauvola");
    }
    if (invocation.fixed_threshold) {
        return core::failure(core::ErrorCode::argument,
                             "--fixed-threshold requires --binarize fixed");
    }
    const auto window = window_value(invocation.sauvola_window);
    if (!window) {
        return std::unexpected(window.error());
    }
    const auto k = value_or(invocation.sauvola_k, methods::Sauvola::default_k, 0.0);
    if (!k) {
        return std::unexpected(k.error());
    }
    const auto r =
        value_or(invocation.sauvola_r, methods::Sauvola::default_r, methods::Sauvola::min_r);
    if (!r) {
        return std::unexpected(r.error());
    }
    return methods::Sauvola::create({.window = *window, .k = *k, .r = *r})
        .transform([](auto value) -> methods::Binarization { return value; });
}
template <typename Value, std::size_t Size>
core::Result<Value> choice(const std::optional<std::string>& raw,
                           const std::array<std::pair<std::string_view, Value>, Size>& choices) {
    if (!raw) {
        return choices.front().second;
    }
    for (const auto& [name, value] : choices) {
        if (*raw == name) {
            return value;
        }
    }
    return core::failure(core::ErrorCode::argument, "Invalid or empty output policy value");
}
core::Result<image::QuarterTurn> prepare_rotation(const contract::Invocation& invocation) {
    const auto value = invocation.rotate.value_or("0");
    for (const auto degrees : {
             0U,
             image::QuarterTurn::quarter_degrees,
             image::QuarterTurn::half_degrees,
             image::QuarterTurn::three_quarter_degrees,
         }) {
        if (value == std::to_string(degrees)) {
            const auto rotation = image::QuarterTurn::from_degrees(degrees);
            if (rotation) {
                return *rotation;
            }
        }
    }
    return core::failure(core::ErrorCode::argument, "--rotate requires 0, 90, 180 or 270");
}
core::Result<image::Continuous> prepare_tone(const contract::Invocation& invocation) {
    using image::AlphaPolicy;
    using image::OutputDepth;
    using image::ProfilePolicy;
    constexpr std::array<std::pair<std::string_view, OutputDepth>, 3> depths{
        {
            std::pair<std::string_view, OutputDepth>{"auto", OutputDepth::automatic},
            {"8", OutputDepth::byte},
            {"16", OutputDepth::word},
        },
    };
    constexpr std::array<std::pair<std::string_view, AlphaPolicy>, 3> alphas{
        {
            std::pair<std::string_view, AlphaPolicy>{"white", AlphaPolicy::white},
            {"black", AlphaPolicy::black},
            {"reject", AlphaPolicy::reject},
        },
    };
    constexpr std::array<std::pair<std::string_view, ProfilePolicy>, 2> profiles{
        {
            std::pair<std::string_view, ProfilePolicy>{"embedded", ProfilePolicy::embedded},
            {"srgb", ProfilePolicy::srgb},
        },
    };
    const auto depth = choice(invocation.bit_depth, depths);
    if (!depth) {
        return std::unexpected(depth.error());
    }
    const auto alpha = choice(invocation.alpha, alphas);
    if (!alpha) {
        return std::unexpected(alpha.error());
    }
    const auto profile = choice(invocation.profile_policy, profiles);
    if (!profile) {
        return std::unexpected(profile.error());
    }
    return image::Continuous::create({
        .mode =
            invocation.output_mode == "gray" ? image::ToneMode::gray : image::ToneMode::preserve,
        .depth = *depth,
        .alpha = *alpha,
        .profile = *profile,
    });
}
core::Result<Operation> prepare_operation(const contract::Invocation& invocation) {
    const auto mode = invocation.output_mode.value_or("preserve");
    if (mode == "bw") {
        if (invocation.bit_depth || invocation.alpha || invocation.profile_policy) {
            return core::failure(core::ErrorCode::argument,
                                 "Binary output retains its stored-sample/8-bit contract; "
                                 "continuous-tone policies are not applicable");
        }
        return prepare_method(invocation).transform([](auto value) -> Operation { return value; });
    }
    if (mode != "preserve" && mode != "gray") {
        return core::failure(core::ErrorCode::argument,
                             "--output-mode requires preserve, gray or bw");
    }
    if (invocation.binarize || invocation.fixed_threshold || invocation.sauvola_window ||
        invocation.sauvola_k || invocation.sauvola_r) {
        return core::failure(core::ErrorCode::argument,
                             "Binarization requires explicit --output-mode bw");
    }
    return prepare_tone(invocation).transform([](auto value) -> Operation { return value; });
}
} // namespace
core::Result<ProcessRequest> prepare_process(const contract::Invocation& invocation) {
    if (invocation.command != contract::Command::process) {
        return core::failure(core::ErrorCode::argument, "Expected a process invocation");
    }
    if (invocation.subject.empty()) {
        return core::failure(core::ErrorCode::argument, "INPUT is required");
    }
    if (invocation.output_directory.empty()) {
        return core::failure(core::ErrorCode::argument, "--out-dir is required");
    }
    if (invocation.subject.contains('\0') || invocation.output_directory.contains('\0')) {
        return core::failure(core::ErrorCode::argument, "Paths cannot contain NUL bytes");
    }
    if (!core::valid_utf8(invocation.subject) || !core::valid_utf8(invocation.output_directory)) {
        return core::failure(core::ErrorCode::argument, "Paths must be well-formed UTF-8");
    }
    const auto rotation = prepare_rotation(invocation);
    if (!rotation) {
        return std::unexpected(rotation.error());
    }
    auto method = prepare_operation(invocation);
    if (!method) {
        return std::unexpected(method.error());
    }
    const auto illumination = prepare_illumination(invocation);
    if (!illumination) {
        return std::unexpected(illumination.error());
    }
    const auto denoising = prepare_denoising(invocation);
    if (!denoising) {
        return std::unexpected(denoising.error());
    }
    auto restoration = prepare_restoration(invocation);
    if (!restoration) {
        return std::unexpected(restoration.error());
    }
    const auto contrast = prepare_contrast(invocation);
    if (!contrast) {
        return std::unexpected(contrast.error());
    }
    const auto sharpening = prepare_sharpening(invocation);
    if (!sharpening) {
        return std::unexpected(sharpening.error());
    }
    return ProcessRequest{invocation.subject,
                          invocation.output_directory,
                          *method,
                          {
                              .illumination = *illumination,
                              .denoising = *denoising,
                              .restoration = std::move(*restoration),
                              .contrast = *contrast,
                              .sharpening = *sharpening,
                              .rotation = {.rotation = *rotation},
                          },
                          invocation.protect_mask};
}
} // namespace docenhance::app
