// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"
#include "read_fields.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
namespace docenhance::bundle {
namespace {
core::Result<void> invalid() {
    return core::failure(core::ErrorCode::input,
                         "The run record contains invalid or inconsistent claims");
}
bool valid_shape(image::RasterShape s) {
    return s.width != 0 && s.height != 0 &&
           (s.depth == image::byte_bits || s.depth == image::word_bits) &&
           image::raster_row_bytes(s).has_value();
}
bool observations_agree(const DeclaredBundle& d) {
    if (!d.operation) {
        return false;
    }
    const auto& o = d.output;
    const auto pixels = std::uint64_t{o.shape.width} * o.shape.height;
    const auto& r = d.illumination;
    const bool binary = std::holds_alternative<methods::Binarization>(*d.operation);
    if (!valid_shape(o.shape) || image::has_alpha(o.shape.model) ||
        (o.resolution && (o.resolution->x == 0 || o.resolution->y == 0))) {
        return false;
    }
    if (binary) {
        return !d.conversion && !d.protection && !r.requested && !r.complete &&
               r.eligible_samples == 0 && o.shape.model == image::SampleModel::gray &&
               o.shape.depth == image::byte_bits && !o.profile_embedded && !o.resolution;
    }
    if (!d.conversion || !o.profile_embedded || !r.complete ||
        r.status == methods::SurfaceStatus::failed) {
        return false;
    }
    const auto& c = *d.conversion;
    return c.output == o.shape && c.resolution == o.resolution && r.protected_samples <= pixels &&
           r.eligible_samples == pixels - r.protected_samples &&
           image::valid_conversion(c, std::get<image::Continuous>(*d.operation));
}
OutputFacts output(const RecordJson& v) {
    return {
        .artifact = {.name = record_text(record_field(v, "path")), .identity = record_identity(v)},
        .shape = record_shape(v),
        .profile_embedded = record_boolean(record_field(v, "profile_embedded")),
        .resolution = record_resolution(record_field(v, "resolution")),
        .verification = Verification::decoded_and_compared,
    };
}
std::optional<ProtectionFacts> protection(const RecordJson& v) {
    if (v.is_null()) {
        return std::nullopt;
    }
    const auto& stored = record_field(v, "stored");
    return ProtectionFacts{
        .original = record_identity(record_field(v, "supplied")),
        .stored =
            {
                .name = record_text(record_field(stored, "path")),
                .identity = record_identity(stored),
            },
        .width = static_cast<std::uint32_t>(record_integer(record_field(v, "width"), UINT32_MAX)),
        .height = static_cast<std::uint32_t>(record_integer(record_field(v, "height"), UINT32_MAX)),
    };
}
core::Result<void> canonical_claims(const RecordJson& document, const DeclaredBundle& d) {
    if (!d.operation) {
        return invalid();
    }
    const RunRecord record{
        .context = {.identity = d.run, .recorded = d.recorded},
        .build =
            {
                .version = d.build.version,
                .platform = d.build.platform,
                .compiler = d.build.compiler,
                .dependency_lock_sha256 = d.build.dependency_lock_sha256,
            },
        .source = d.source,
        .operation = *d.operation,
        .protection_supplied = d.protection_supplied,
        .output = d.output,
        .protection = d.protection,
        .conversion = d.conversion,
        .illumination = d.illumination,
        .denoising = d.denoising,
    };
    auto canonical = serialize(record);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    // Equality is structural, not text or key order. This checks every required/unknown field,
    // derived fraction, warning and method identity without a second default table.
    return RecordJson::parse(*canonical) == document ? core::Result<void>{} : invalid();
}
} // namespace
core::Result<void> validate_record_claims(const RecordJson& document, DeclaredBundle& d) {
    const auto& build = record_field(document, "build");
    const auto version = record_text(record_field(build, "application_version"));
    const auto platform = record_text(record_field(build, "platform"));
    const auto compiler = record_text(record_field(build, "compiler"));
    const auto lock = record_text(record_field(build, "dependency_lock_sha256"));
    const auto& source = record_field(document, "source");
    auto source_facts = record_source_facts(source);
    if (!source_facts) {
        return invalid();
    }
    const auto& request = record_field(document, "request");
    auto op = record_operation(record_field(request, "operation"));
    auto light =
        record_illumination(record_field(record_field(document, "execution"), "illumination"));
    if (!op || !light) {
        return invalid();
    }
    auto denoise_claims = validate_denoising_claims(document, d, *op, *light);
    if (!denoise_claims) {
        return denoise_claims;
    }
    d.operation = *op;
    d.illumination = *light;
    const auto& converted = record_field(record_field(document, "execution"), "conversion");
    if (!converted.is_null()) {
        auto c = record_conversion(converted);
        if (!c) {
            return invalid();
        }
        d.conversion = *c;
    }
    d.output = output(record_field(document, "output"));
    d.protection = protection(record_field(document, "protection"));
    const bool supplied = record_boolean(record_field(request, "protection_supplied"));
    d.build = {
        .version = version,
        .platform = platform,
        .compiler = compiler,
        .dependency_lock_sha256 = lock,
    };
    d.source = *source_facts;
    d.protection_supplied = supplied;
    if (version.empty() || platform.empty() || compiler.empty() ||
        !core::valid_hexadecimal(lock, core::sha256_hex_length) ||
        !core::valid_hexadecimal(source_facts->identity.sha256, core::sha256_hex_length) ||
        source_facts->identity.bytes == 0 || source_facts->name.empty() ||
        source_facts->name.contains('\0') || source_facts->name.contains('/') ||
        !core::valid_hexadecimal(d.run, core::run_identity_hex_length) ||
        !core::valid_instant(d.recorded) || supplied != d.protection.has_value() ||
        d.output.artifact.name != image_name || !observations_agree(d) || !illumination_agrees(d) ||
        !source_agrees(d)) {
        return invalid();
    }
    if (d.protection &&
        (d.protection->stored.name != mask_name ||
         !core::valid_hexadecimal(d.protection->original.sha256, core::sha256_hex_length) ||
         d.protection->original.bytes == 0 || d.protection->width != d.output.shape.width ||
         d.protection->height != d.output.shape.height)) {
        return invalid();
    }
    return canonical_claims(document, d);
}
} // namespace docenhance::bundle
