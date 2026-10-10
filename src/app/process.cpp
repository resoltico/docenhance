// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/process.hpp"

#include "contrast.hpp"
#include "denoising.hpp"
#include "docenhance/contract/cli_contract.hpp"
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace docenhance::app {
namespace {
core::Error context(const contract::Invocation& invocation, core::Error error,
                    std::string_view group) {
    if (error.code != core::ErrorCode::argument || error.message.contains("domain/default:")) {
        return error;
    }
    for (const auto& option : contract::option_catalog) {
        if (group.empty()
                ? (option.group != "Processing arguments" && option.group != "Output arguments")
                : option.group != group) {
            continue;
        }
        const auto* const member =
            std::get_if<std::optional<std::string> contract::Invocation::*>(&option.binding);
        if (member == nullptr) {
            continue;
        }
        const auto& raw = invocation.**member;
        if (!raw) {
            continue;
        }
        error.message += "\n" + contract::option_error({
                                                           .name = option.name,
                                                           .value = *raw,
                                                           .reason = "Supplied option",
                                                       })
                                    .message;
    }
    return error;
}
core::Result<double> value_or(const std::optional<std::string>& value, std::string_view name,
                              double fallback, double low) {
    return value ? contract::parse_decimal_option(name, *value, low, 1.0)
                 : core::Result<double>{fallback};
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
        const auto threshold = value_or(invocation.fixed_threshold, "--fixed-threshold",
                                        methods::FixedThreshold::default_threshold, 0.0);
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
    const auto window = (invocation.sauvola_window
                             ? contract::parse_integer_option(
                                   "--sauvola-window", *invocation.sauvola_window,
                                   methods::Sauvola::min_window, methods::Sauvola::max_window, true)
                             : core::Result<std::uint32_t>{methods::Sauvola::default_window});
    if (!window) {
        return std::unexpected(window.error());
    }
    const auto k = value_or(invocation.sauvola_k, "--sauvola-k", methods::Sauvola::default_k, 0.0);
    if (!k) {
        return std::unexpected(k.error());
    }
    const auto r = value_or(invocation.sauvola_r, "--sauvola-r", methods::Sauvola::default_r,
                            methods::Sauvola::min_r);
    if (!r) {
        return std::unexpected(r.error());
    }
    return methods::Sauvola::create({.window = *window, .k = *k, .r = *r})
        .transform([](auto value) -> methods::Binarization { return value; });
}
template <typename Value, std::size_t Size>
core::Result<Value> choice(const std::optional<std::string>& raw, std::string_view option,
                           const std::array<std::pair<std::string_view, Value>, Size>& choices) {
    if (!raw) {
        return choices.front().second;
    }
    for (const auto& [name, value] : choices) {
        if (*raw == name) {
            return value;
        }
    }
    return std::unexpected(
        contract::option_error({.name = option, .value = *raw, .reason = "Invalid output policy"}));
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
    return std::unexpected(contract::option_error(
        {.name = "--rotate", .value = value, .reason = "Expected an exact quarter-turn"}));
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
    const auto depth = choice(invocation.bit_depth, "--bit-depth", depths);
    if (!depth) {
        return std::unexpected(depth.error());
    }
    const auto alpha = choice(invocation.alpha, "--alpha", alphas);
    if (!alpha) {
        return std::unexpected(alpha.error());
    }
    const auto profile = choice(invocation.profile_policy, "--profile-policy", profiles);
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
        return std::unexpected(context(invocation, method.error(), ""));
    }
    const auto illumination = prepare_illumination(invocation);
    if (!illumination) {
        return std::unexpected(context(invocation, illumination.error(), "Illumination arguments"));
    }
    const auto denoising = prepare_denoising(invocation);
    if (!denoising) {
        return std::unexpected(context(invocation, denoising.error(), "Denoising arguments"));
    }
    auto restoration = prepare_restoration(invocation);
    if (!restoration) {
        return std::unexpected(context(invocation, restoration.error(), "Restoration arguments"));
    }
    const auto contrast = prepare_contrast(invocation);
    if (!contrast) {
        return std::unexpected(context(invocation, contrast.error(), "Contrast arguments"));
    }
    const auto sharpening = prepare_sharpening(invocation);
    if (!sharpening) {
        return std::unexpected(context(invocation, sharpening.error(), "Sharpening arguments"));
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
