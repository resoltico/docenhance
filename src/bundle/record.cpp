// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/record.hpp"

#include "docenhance/bundle/fields.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/catalog.hpp"

#include <array>
#include <new>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace docenhance::bundle {
using Json = nlohmann::ordered_json;
namespace {
constexpr int record_indent = 2;

std::string_view tone_mode_name(image::ToneMode mode) noexcept {
    return mode == image::ToneMode::gray ? "gray" : "preserve";
}
std::string_view depth_name(image::OutputDepth depth) noexcept {
    switch (depth) {
    case image::OutputDepth::byte:
        return "8";
    case image::OutputDepth::word:
        return "16";
    case image::OutputDepth::automatic:
        return "automatic";
    }
    return "automatic";
}
std::string_view alpha_name(image::AlphaPolicy alpha) noexcept {
    switch (alpha) {
    case image::AlphaPolicy::black:
        return "black";
    case image::AlphaPolicy::reject:
        return "reject";
    case image::AlphaPolicy::white:
        return "white";
    }
    return "white";
}
std::string_view profile_name(image::ProfilePolicy profile) noexcept {
    return profile == image::ProfilePolicy::srgb ? "srgb" : "embedded";
}
Json method_identity(methods::ImplementedMethod method) {
    return {{"id", method.id}, {"method_version", method.method_version}};
}
Json binarization_fields(const methods::Binarization& method) {
    Json parameters = std::visit(
        [](const auto& value) -> Json {
            using Method = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Method, methods::Sauvola>) {
                return {{"window", value.window()}, {"k", value.k()}, {"r", value.r()}};
            } else {
                return {{"threshold", value.threshold()}};
            }
        },
        method);
    return {
        {"kind", "binarization"},
        {"method", method_identity(methods::describe(method))},
        {"parameters", parameters},
    };
}
Json continuous_request_fields(const image::Continuous& operation) {
    const auto parameters = operation.parameters();
    return {
        {"kind", "continuous"},
        {"method", nullptr},
        {
            "parameters",
            {
                {"output_mode", tone_mode_name(parameters.mode)},
                {"depth", depth_name(parameters.depth)},
                {"alpha", alpha_name(parameters.alpha)},
                {"profile", profile_name(parameters.profile)},
            },
        },
    };
}
Json operation_fields(const Operation& operation) {
    return std::visit(
        [](const auto& value) -> Json {
            using Kind = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Kind, methods::Binarization>) {
                return binarization_fields(value);
            } else {
                return continuous_request_fields(value);
            }
        },
        operation);
}
Json identity_fields(const core::ContentIdentity& identity) {
    return {{"sha256", identity.sha256}, {"bytes", identity.bytes}};
}
Json artifact_fields(const Artifact& artifact) {
    Json fields = {{"path", artifact.name}};
    fields.update(identity_fields(artifact.identity));
    return fields;
}
// Naming what was compared, so a later reader cannot read more into the word "verified" than a
// decode-back comparison establishes.
Json verification_fields(Verification verification) {
    if (verification == Verification::not_performed) {
        return {{"performed", "none"}, {"compared", Json::array()}};
    }
    return {
        {"performed", "decoded_and_compared"},
        {"compared", std::array<std::string_view, 2>{"integer_samples", "metadata"}},
    };
}
Json output_fields(const OutputFacts& output) {
    Json resolution = nullptr;
    if (output.resolution) {
        resolution = {{"x_ppm", output.resolution->x}, {"y_ppm", output.resolution->y}};
    }
    Json fields = artifact_fields(output.artifact);
    fields.update(Json{
        {"width", output.shape.width},
        {"height", output.shape.height},
        {"channels", image::components(output.shape.model)},
        {"bit_depth", output.shape.depth},
        {"profile_embedded", output.profile_embedded},
        {"resolution", resolution},
        {"verification", verification_fields(output.verification)},
    });
    return fields;
}
Json protection_fields(const ProtectionFacts& protection) {
    return {
        {"supplied", identity_fields(protection.original)},
        {"stored", artifact_fields(protection.stored)},
        {"width", protection.width},
        {"height", protection.height},
        {"polarity", "white_protects"},
        {"frame", "oriented_source"},
    };
}
Json build_fields(const core::BuildFacts& build) {
    return {
        {"application_version", build.version},
        {"platform", build.platform},
        {"compiler", build.compiler},
        {"dependency_lock_sha256", build.dependency_lock_sha256},
    };
}
} // namespace

core::Result<std::string> serialize(const RunRecord& record) {
    try {
        Json source = identity_fields(record.source.identity);
        source.update(Json{{"name", record.source.name}});
        if (record.source.decoding) {
            source.emplace("decoding", source_fields(*record.source.decoding));
        }
        const Json document = {
            {
                "record",
                {
                    {"version", record.source.decoding ? record_version : 1},
                    {"run", record.context.identity},
                    {"recorded", record.context.recorded},
                },
            },
            {"build", build_fields(record.build)},
            {"source", source},
            {
                "request",
                {
                    {"operation", operation_fields(record.operation)},
                    {"protection_supplied", record.protection_supplied},
                },
            },
            {
                "execution",
                {
                    {
                        "conversion",
                        record.conversion ? conversion_fields(*record.conversion) : Json(nullptr),
                    },
                    {"illumination", illumination_fields(record.illumination)},
                },
            },
            {
                "protection",
                record.protection ? protection_fields(*record.protection) : Json(nullptr),
            },
            {"output", output_fields(record.output)},
        };
        return document.dump(record_indent, ' ', false, Json::error_handler_t::strict) + "\n";
    } catch (const Json::exception&) {
        return core::failure(core::ErrorCode::invariant,
                             "The run record contains malformed identity text");
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource, "Writing the run record exhausted memory");
    }
}
} // namespace docenhance::bundle
