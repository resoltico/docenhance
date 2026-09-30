// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/catalog.hpp"
#include "read_fields.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
namespace docenhance::bundle {
constexpr unsigned last_orientation = 8;
namespace {
core::Result<Operation> binary(const RecordJson& value) {
    const auto& method = record_field(value, "method");
    const auto& parameters = record_field(value, "parameters");
    const auto id = record_text(record_field(method, "id"));
    const auto version = record_integer(record_field(method, "method_version"), UINT32_MAX);
    if (id == methods::FixedThreshold::descriptor().id &&
        version == methods::FixedThreshold::descriptor().method_version) {
        auto admitted =
            methods::FixedThreshold::create(record_number(record_field(parameters, "threshold")));
        if (admitted) {
            return Operation{methods::Binarization{*admitted}};
        }
    }
    if (id == methods::Sauvola::descriptor().id &&
        version == methods::Sauvola::descriptor().method_version) {
        auto admitted = methods::Sauvola::create({
            .window = static_cast<std::uint32_t>(
                record_integer(record_field(parameters, "window"), UINT32_MAX)),
            .k = record_number(record_field(parameters, "k")),
            .r = record_number(record_field(parameters, "r")),
        });
        if (admitted) {
            return Operation{methods::Binarization{*admitted}};
        }
    }
    return core::failure(core::ErrorCode::input, "Invalid recorded binarization");
}
core::Result<Operation> continuous(const RecordJson& value) {
    const auto& p = record_field(value, "parameters");
    const auto mode = record_text(record_field(p, "output_mode"));
    const auto depth = record_text(record_field(p, "depth"));
    const auto alpha = record_text(record_field(p, "alpha"));
    const auto profile = record_text(record_field(p, "profile"));
    auto depth_policy = image::OutputDepth::automatic;
    if (depth == "8") {
        depth_policy = image::OutputDepth::byte;
    }
    if (depth == "16") {
        depth_policy = image::OutputDepth::word;
    }
    auto alpha_policy = image::AlphaPolicy::white;
    if (alpha == "black") {
        alpha_policy = image::AlphaPolicy::black;
    }
    if (alpha == "reject") {
        alpha_policy = image::AlphaPolicy::reject;
    }
    const image::ToneParameters parameters{
        .mode = mode == "gray" ? image::ToneMode::gray : image::ToneMode::preserve,
        .depth = depth_policy,
        .alpha = alpha_policy,
        .profile = profile == "srgb" ? image::ProfilePolicy::srgb : image::ProfilePolicy::embedded,
    };
    auto admitted = image::Continuous::create(parameters);
    if (!admitted) {
        return std::unexpected(admitted.error());
    }
    // The canonical comparison rejects every unrecognized spelling, including those mapped
    // to a fallback enum above. These values never reach execution.
    return Operation{*admitted};
}
} // namespace
core::Result<Operation> record_operation(const RecordJson& value) {
    const auto kind = record_text(record_field(value, "kind"));
    if (kind == "binarization") {
        return binary(value);
    }
    if (kind == "continuous") {
        return continuous(value);
    }
    return core::failure(core::ErrorCode::input, "Unsupported recorded operation");
}
core::Result<image::ConversionReport> record_conversion(const RecordJson& value) {
    constexpr auto interpretations = std::to_array({
        image::Interpretation::assumed_srgb,
        image::Interpretation::overridden_srgb,
        image::Interpretation::srgb,
        image::Interpretation::cicp,
        image::Interpretation::icc,
        image::Interpretation::gamma,
        image::Interpretation::chromaticities,
    });
    const auto decision = record_text(record_field(value, "profile_decision"));
    for (const auto interpretation : interpretations) {
        if (image::interpretation_name(interpretation) == decision) {
            return image::ConversionReport{
                .source = record_shape(record_field(value, "decoded_input")),
                .output = record_shape(record_field(value, "encoded_output")),
                .interpretation = interpretation,
                .orientation = static_cast<unsigned>(
                    record_integer(record_field(value, "source_orientation"), last_orientation)),
                .resolution = record_resolution(record_field(value, "resolution")),
                .flattened_pixels =
                    record_integer(record_field(value, "alpha_flattened_pixels"), UINT64_MAX),
                .clipped_components =
                    record_integer(record_field(value, "clipped_components"), UINT64_MAX),
                .assumed_transfer = record_boolean(record_field(value, "assumed_transfer")),
                .assumed_primaries = record_boolean(record_field(value, "assumed_primaries")),
                .depth_reduced = record_boolean(record_field(value, "depth_reduced")),
                .verified = record_boolean(record_field(value, "verified")),
            };
        }
    }
    return core::failure(core::ErrorCode::input, "Unsupported recorded profile decision");
}
} // namespace docenhance::bundle
