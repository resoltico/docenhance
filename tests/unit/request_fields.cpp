// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/record/request_fields.hpp"

#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/schema.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
// NOLINTNEXTLINE(misc-include-cleaner)
#include <variant>
namespace docenhance::tests {
TEST_CASE("Persistent binary request captures every common photometric selection") {
    const auto surface = methods::Surface::create();
    const auto nlm = methods::Nlm::create();
    const auto wiener = methods::Wiener::create();
    const auto gamma = methods::Gamma::create();
    const auto unsharp = methods::Unsharp::create();
    const auto fixed = methods::FixedThreshold::create();
    REQUIRE(surface);
    REQUIRE(nlm);
    REQUIRE(wiener);
    REQUIRE(gamma);
    REQUIRE(unsharp);
    REQUIRE(fixed);
    const auto rotation = image::QuarterTurn::from_degrees(90);
    REQUIRE(rotation.has_value());
    if (!rotation.has_value()) {
        throw std::logic_error("Required quarter-turn fixture is absent");
    }
    const methods::Plan plan{
        .alpha = image::AlphaPolicy::black,
        .rotation = *rotation,
        .photometric =
            {
                .illumination = *surface,
                .denoising = *nlm,
                .restoration = *wiener,
                .contrast = *gamma,
                .sharpening = *unsharp,
            },
        .output = methods::BinaryOutput{.method = *fixed},
        .protection = true,
    };
    const core::ContentIdentity identity{.sha256 = std::string(64, 'a'), .bytes = 71};
    const auto request = record::requested(plan, identity);
    REQUIRE(request);
    const auto encoding = record::Codec::encode(*request);
    REQUIRE(encoding);
    const auto& encoded = *encoding;
    REQUIRE((request->illumination && request->denoising && request->restoration &&
             request->contrast && request->sharpening && request->binarization &&
             request->protection));
    if (!request->illumination.has_value() || !request->denoising.has_value() ||
        !request->restoration.has_value() || !request->contrast.has_value() ||
        !request->sharpening.has_value() || !request->binarization.has_value() ||
        !request->protection.has_value()) {
        throw std::logic_error("Required persistent request selections are absent");
    }
    CHECK(std::get<record::SurfaceRequest>(*request->illumination).selector ==
          surface->parameters().mode);
    const auto& light = encoded["illumination"]["parameters"];
    CHECK(light["strength"] == surface->parameters().strength);
    CHECK(light["max_gain"] == surface->parameters().max_gain);
    const auto& surface_parameters =
        std::get<record::SurfaceRequest>(*request->illumination).parameters;
    CHECK(surface_parameters.target == surface->parameters().target);
    CHECK(surface_parameters.cell == surface->parameters().cell);
    CHECK(!surface->parameters().target.has_value());
    CHECK(!surface->parameters().cell.has_value());
    CHECK(light["target"] == "source");
    CHECK(light["cell"] == "auto");
    CHECK(light["quantile"] == surface->parameters().quantile);
    CHECK(light["smooth"] == surface->parameters().smooth);
    CHECK(std::get<record::NlmRequest>(*request->denoising).parameters == nlm->parameters());
    CHECK(request->restoration->parameters == wiener->parameters());
    CHECK(std::get<record::GammaRequest>(*request->contrast).parameters == gamma->parameters());
    CHECK(request->sharpening->parameters == unsharp->parameters());
    CHECK(std::get<record::FixedThresholdRequest>(*request->binarization).parameters.threshold ==
          fixed->threshold());
    CHECK(std::get<record::BinaryOutputRequest>(request->output).profile_policy == plan.profile);
    CHECK(request->protection->supplied == identity);
    CHECK(encoded["output"]["mode"] == "bw");
    CHECK(encoded["output"]["depth"].is_null());
    CHECK(encoded["output"]["alpha"] == "black");
    CHECK(encoded["rotation"] == 90);
    CHECK(encoded["protection"]["supplied"]["sha256"] == identity.sha256);
    for (const auto* const family :
         {"illumination", "denoising", "restoration", "contrast", "sharpening", "binarization"}) {
        CHECK(encoded[family].size() == 4);
        CHECK(encoded[family].contains("parameters"));
        CHECK(encoded[family].contains("selector"));
    }
    const auto restored = record::Codec::decode<record::Request>(encoded);
    REQUIRE(restored);
    CHECK(*restored == *request);
    const auto refused = record::requested(plan, std::nullopt);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::invariant);
    auto invalid = encoded;
    invalid["binarization"] = nullptr;
    CHECK(!record::Codec::decode<record::Request>(invalid));
    invalid = encoded;
    invalid["illumination"]["version"] = 100;
    CHECK(!record::Codec::decode<record::Request>(invalid));
    invalid = encoded;
    invalid["illumination"]["parameters"]["selector"] = "surface";
    CHECK(!record::Codec::decode<record::Request>(invalid));
}
TEST_CASE("Persistent output alternatives encode exact depth nullability") {
    for (const auto mode : {image::ToneMode::preserve, image::ToneMode::gray}) {
        for (const auto depth :
             {image::OutputDepth::automatic, image::OutputDepth::byte, image::OutputDepth::word}) {
            const methods::Plan plan{
                .rotation = image::QuarterTurn(),
                .photometric = methods::Photometric(),
                .output = methods::ContinuousOutput{.mode = mode, .depth = depth},
            };
            const auto request = record::requested(plan, std::nullopt);
            REQUIRE(request);
            const auto encoding = record::Codec::encode(*request);
            REQUIRE(encoding);
            const auto& encoded = *encoding;
            CHECK(encoded["binarization"].is_null());
            CHECK(!encoded["output"]["depth"].is_null());
            CHECK(record::Codec::decode<record::Request>(encoded));
            auto invalid = encoded;
            invalid["output"]["depth"] = nullptr;
            CHECK(!record::Codec::decode<record::Request>(invalid));
            invalid = encoded;
            invalid["output"]["depth"] = "1";
            CHECK(!record::Codec::decode<record::Request>(invalid));
            invalid["output"]["depth"] = 8;
            CHECK(!record::Codec::decode<record::Request>(invalid));
        }
    }
    const methods::Plan binary{
        .rotation = image::QuarterTurn(),
        .photometric = methods::Photometric(),
        .output = methods::BinaryOutput{.method = methods::Otsu::create()},
    };
    const auto request = record::requested(binary, std::nullopt);
    REQUIRE(request);
    const auto encoding = record::Codec::encode(*request);
    REQUIRE(encoding);
    const auto& encoded = *encoding;
    for (const auto& depth : record::Json::array({"auto", "8", "16", 1})) {
        auto invalid = encoded;
        invalid["output"]["depth"] = depth;
        CHECK(!record::Codec::decode<record::Request>(invalid));
    }
    auto invalid = encoded;
    invalid["output"]["mode"] = "gray";
    invalid["output"]["depth"] = "8";
    CHECK(!record::Codec::decode<record::Request>(invalid));
    const auto schema = record::Schema::of<record::BinaryOutputRequest>();
    REQUIRE(schema);
    CHECK((*schema)["properties"]["depth"]["type"] == "null");
}
TEST_CASE("Persistent protection identity and kernel selections retain distinct resource roles") {
    auto wiener = methods::Wiener::create({.psf = methods::Kernel{}});
    REQUIRE(wiener);
    const methods::Plan plan{
        .rotation = image::QuarterTurn(),
        .photometric =
            {
                .illumination = methods::IlluminationOff(),
                .denoising = methods::DenoisingOff(),
                .restoration = *wiener,
                .contrast = methods::ContrastOff(),
                .sharpening = methods::SharpenOff(),
            },
        .output = methods::ContinuousOutput(),
        .protection = true,
    };
    const core::ContentIdentity identity{
        .sha256 = std::string(64, 'b'),
        .bytes = core::source_encoded_bytes_max,
    };
    const auto request = record::requested(plan, identity);
    REQUIRE(request);
    const auto encoding = record::Codec::encode(*request);
    REQUIRE(encoding);
    const auto& encoded = *encoding;
    CHECK(encoded["restoration"]["parameters"]["psf"].size() == 1);
    CHECK(encoded["restoration"]["parameters"]["psf"]["kind"] == "kernel");
    CHECK(!encoded["restoration"]["parameters"]["psf"].contains("path"));
    auto invalid = encoded;
    invalid["restoration"]["parameters"]["psf"]["path"] = "private/kernel.png";
    CHECK(!record::Codec::decode<record::Request>(invalid));
    for (const auto size : {std::uint64_t{0}, core::source_encoded_bytes_max + 1}) {
        invalid = encoded;
        invalid["protection"]["supplied"]["bytes"] = size;
        CHECK(!record::Codec::decode<record::Request>(invalid));
    }
    invalid = encoded;
    invalid["protection"]["supplied"]["sha256"] = std::string(64, 'B');
    CHECK(!record::Codec::decode<record::Request>(invalid));
    invalid = encoded;
    invalid["restoration"]["parameters"]["psf"] = {{"kind", "file"}, {"path", "kernel.png"}};
    CHECK(!record::Codec::decode<record::Request>(invalid));
}
TEST_CASE("Persistent request factories refuse invalid in-memory plan and supplied facts") {
    methods::Plan plan;
    REQUIRE(methods::validate(plan));
    REQUIRE(record::requested(plan, std::nullopt));
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    plan.alpha = static_cast<image::AlphaPolicy>(-1);
    const auto invalid_plan = record::requested(plan, std::nullopt);
    REQUIRE(!invalid_plan);
    CHECK(invalid_plan.error().code == core::ErrorCode::invariant);
    plan.alpha = image::AlphaPolicy::white;
    plan.protection = true;
    for (const auto size : {std::uint64_t{0}, core::source_encoded_bytes_max + 1}) {
        const core::ContentIdentity identity{.sha256 = std::string(64, 'a'), .bytes = size};
        const auto invalid_identity = record::requested(plan, identity);
        REQUIRE(!invalid_identity);
        CHECK(invalid_identity.error().code == core::ErrorCode::invariant);
    }
    const core::ContentIdentity identity{.sha256 = std::string(64, 'A'), .bytes = 71};
    const auto invalid_digest = record::requested(plan, identity);
    REQUIRE(!invalid_digest);
    CHECK(invalid_digest.error().code == core::ErrorCode::invariant);
}
} // namespace docenhance::tests
