// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/record/request.hpp"

#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/record/method_requests.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::record {
namespace {
SurfaceRequest selection(const methods::Surface& value) {
    const auto p = value.parameters();
    return {
        .selector = p.mode,
        .parameters =
            {
                .strength = p.strength,
                .max_gain = p.max_gain,
                .target = p.target,
                .cell = p.cell,
                .quantile = p.quantile,
                .smooth = p.smooth,
            },
    };
}
MorphologyRequest selection(const methods::Morphology& value) {
    return {.parameters = value.parameters()};
}
NlmRequest selection(const methods::Nlm& value) {
    return {.parameters = value.parameters()};
}
Tvl1Request selection(const methods::Tvl1& value) {
    return {.parameters = value.parameters()};
}
WienerRequest selection(const methods::Wiener& value) {
    return {.parameters = value.parameters()};
}
LevelsRequest selection(const methods::Levels& value) {
    return {.parameters = value.parameters()};
}
GammaRequest selection(const methods::Gamma& value) {
    return {.parameters = value.parameters()};
}
ClaheRequest selection(const methods::Clahe& value) {
    return {.parameters = value.parameters()};
}
UnsharpRequest selection(const methods::Unsharp& value) {
    return {.parameters = value.parameters()};
}
OtsuRequest selection(const methods::Otsu& /*value*/) {
    return {};
}
SauvolaRequest selection(const methods::Sauvola& value) {
    return {.parameters = {.window = value.window(), .k = value.k(), .r = value.r()}};
}
FixedThresholdRequest selection(const methods::FixedThreshold& value) {
    return {.parameters = {.threshold = value.threshold()}};
}
template <typename Result, typename Selection>
std::optional<Result> selected(const Selection& value) {
    return std::visit(
        [](const auto& method) -> std::optional<Result> {
            using Method = std::remove_cvref_t<decltype(method)>;
            if constexpr (std::is_same_v<Method, methods::IlluminationOff> ||
                          std::is_same_v<Method, methods::DenoisingOff> ||
                          std::is_same_v<Method, methods::RestorationOff> ||
                          std::is_same_v<Method, methods::ContrastOff> ||
                          std::is_same_v<Method, methods::SharpenOff>) {
                return std::nullopt;
            } else {
                return Result{selection(method)};
            }
        },
        value);
}
OutputRequest selected_output(const methods::Plan& plan) {
    if (const auto* const continuous = std::get_if<methods::ContinuousOutput>(&plan.output)) {
        if (continuous->mode == image::ToneMode::gray) {
            return GrayOutputRequest{
                .depth = continuous->depth,
                .alpha = plan.alpha,
                .profile_policy = plan.profile,
            };
        }
        return PreserveOutputRequest{
            .depth = continuous->depth,
            .alpha = plan.alpha,
            .profile_policy = plan.profile,
        };
    }
    return BinaryOutputRequest{
        .depth = {},
        .alpha = plan.alpha,
        .profile_policy = plan.profile,
    };
}
std::optional<BinarizationRequest> selected_binarization(const methods::Plan& plan) {
    const auto* const binary = std::get_if<methods::BinaryOutput>(&plan.output);
    if (binary == nullptr) {
        return std::nullopt;
    }
    return std::visit([](const auto& method) -> BinarizationRequest { return selection(method); },
                      binary->method);
}
template <typename T> bool admitted(const std::optional<T>& value) {
    if (!value) {
        return true;
    }
    if constexpr (requires { value->valueless_by_exception(); }) {
        return !value->valueless_by_exception() &&
               std::visit([](const auto& method) { return validate(method); }, *value);
    } else {
        return validate(*value);
    }
}
} // namespace
bool validate(const PreserveOutputRequest& value) noexcept {
    return (value.depth == image::OutputDepth::automatic ||
            value.depth == image::OutputDepth::byte || value.depth == image::OutputDepth::word) &&
           image::valid_interpretation_policy(
               {.alpha = value.alpha, .profile = value.profile_policy, .rotation = {}});
}
bool validate(const GrayOutputRequest& value) noexcept {
    return validate(PreserveOutputRequest{
        .depth = value.depth,
        .alpha = value.alpha,
        .profile_policy = value.profile_policy,
    });
}
bool validate(const BinaryOutputRequest& value) noexcept {
    return image::valid_interpretation_policy(
        {.alpha = value.alpha, .profile = value.profile_policy, .rotation = {}});
}
bool validate(const RequestedProtection& value) {
    return value.supplied.bytes != 0 && value.supplied.bytes <= core::source_encoded_bytes_max &&
           core::valid_hexadecimal(value.supplied.sha256, core::sha256_hex_length);
}
bool validate(const Request& value) {
    return !value.output.valueless_by_exception() &&
           std::visit([](const auto& output) { return validate(output); }, value.output) &&
           std::holds_alternative<BinaryOutputRequest>(value.output) ==
               value.binarization.has_value() &&
           admitted(value.protection) && admitted(value.illumination) &&
           admitted(value.denoising) && admitted(value.restoration) && admitted(value.contrast) &&
           admitted(value.sharpening) && admitted(value.binarization);
}
core::Result<Request> requested(const methods::Plan& plan,
                                std::optional<core::ContentIdentity> protection) {
    if (!methods::validate(plan) || plan.protection != protection.has_value()) {
        return core::failure(core::ErrorCode::invariant, "Invalid persistent request input");
    }
    Request result{
        .output = selected_output(plan),
        .rotation = plan.rotation,
        .protection = std::nullopt,
        .illumination = selected<IlluminationRequest>(plan.photometric.illumination),
        .denoising = selected<DenoisingRequest>(plan.photometric.denoising),
        .restoration = selected<WienerRequest>(plan.photometric.restoration),
        .contrast = selected<ContrastRequest>(plan.photometric.contrast),
        .sharpening = selected<UnsharpRequest>(plan.photometric.sharpening),
        .binarization = selected_binarization(plan),
    };
    if (protection) {
        result.protection = RequestedProtection{.supplied = std::move(*protection)};
    }
    if (!validate(result)) {
        return core::failure(core::ErrorCode::invariant, "Invalid persistent request facts");
    }
    return result;
}

} // namespace docenhance::record
