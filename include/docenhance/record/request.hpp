// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/record/method_requests.hpp"

#include <optional>
#include <variant>
namespace docenhance::record {
struct PreserveOutputRequest {
    image::OutputDepth depth = image::OutputDepth::automatic;
    image::AlphaPolicy alpha = image::AlphaPolicy::white;
    image::ProfilePolicy profile_policy = image::ProfilePolicy::embedded;
    bool operator==(const PreserveOutputRequest&) const = default;
};
struct GrayOutputRequest {
    image::OutputDepth depth = image::OutputDepth::automatic;
    image::AlphaPolicy alpha = image::AlphaPolicy::white;
    image::ProfilePolicy profile_policy = image::ProfilePolicy::embedded;
    bool operator==(const GrayOutputRequest&) const = default;
};
struct BinaryOutputRequest {
    std::monostate depth;
    image::AlphaPolicy alpha = image::AlphaPolicy::white;
    image::ProfilePolicy profile_policy = image::ProfilePolicy::embedded;
    bool operator==(const BinaryOutputRequest&) const = default;
};
using OutputRequest = std::variant<PreserveOutputRequest, GrayOutputRequest, BinaryOutputRequest>;
struct RequestedProtection {
    core::ContentIdentity supplied;
    bool operator==(const RequestedProtection&) const = default;
};
using IlluminationRequest = std::variant<SurfaceRequest, MorphologyRequest>;
using DenoisingRequest = std::variant<NlmRequest, Tvl1Request>;
using ContrastRequest = std::variant<LevelsRequest, GammaRequest, ClaheRequest>;
using BinarizationRequest = std::variant<OtsuRequest, SauvolaRequest, FixedThresholdRequest>;
struct Request {
    OutputRequest output;
    image::QuarterTurn rotation;
    std::optional<RequestedProtection> protection;
    std::optional<IlluminationRequest> illumination;
    std::optional<DenoisingRequest> denoising;
    std::optional<WienerRequest> restoration;
    std::optional<ContrastRequest> contrast;
    std::optional<UnsharpRequest> sharpening;
    std::optional<BinarizationRequest> binarization;
    bool operator==(const Request&) const = default;
};
[[nodiscard]] bool validate(const PreserveOutputRequest& value) noexcept;
[[nodiscard]] bool validate(const GrayOutputRequest& value) noexcept;
[[nodiscard]] bool validate(const BinaryOutputRequest& value) noexcept;
[[nodiscard]] bool validate(const RequestedProtection& value);
[[nodiscard]] bool validate(const Request& value);
// Persistent selection contains no source/PSF path. Native resources belong to the host input.
[[nodiscard]] core::Result<Request> requested(const methods::Plan& plan,
                                              std::optional<core::ContentIdentity> protection);
} // namespace docenhance::record
