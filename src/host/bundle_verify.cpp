// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_verify.hpp"

#include "docenhance/codec/png_artifact.hpp"
#include "docenhance/codec/psf_png.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/artifact_limits.hpp"
#include "docenhance/fs/bundle_snapshot.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/fs/publication.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/record/inventory.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
#include <variant>
#include <vector>
namespace docenhance::host {
namespace {
core::Result<void> check_image(const record::RunRecord& declared, const fs::BundleSnapshot& bytes,
                               core::Budget& budget, const core::Cancellation& cancellation) {
    auto image = codec::observe_png_artifact(bytes.image.bytes(), budget, cancellation);
    if (!image) {
        return std::unexpected(std::move(image.error()));
    }
    if (auto profile = color::validate_output_profile(image->shape, image->profile.bytes(), budget,
                                                      cancellation);
        !profile) {
        return profile;
    }
    const auto& output = declared.output;
    const bool binary =
        std::holds_alternative<record::BinaryOutputRequest>(declared.request.output);
    if (image->shape != output.shape() || image->profile_embedded != output.profile_embedded ||
        image->resolution != output.resolution || (binary && !image->binary_samples)) {
        return core::failure(core::ErrorCode::input,
                             "Bundle image contradicts its recorded properties");
    }
    if (binary) {
        if (!declared.execution.binarization) {
            return core::failure(core::ErrorCode::input, "Bundle classification facts are absent");
        }
        const bool agrees = std::visit(
            [&](const auto& facts) { return facts.white_samples == image->white_samples; },
            *declared.execution.binarization);
        if (!agrees) {
            return core::failure(core::ErrorCode::input,
                                 "Bundle image contradicts its classification counts");
        }
    }
    return {};
}
core::Result<void> check_mask(const record::ProtectionAsset& protection,
                              const fs::BundleSnapshot& bytes, core::Budget& budget,
                              const core::Cancellation& cancellation) {
    auto mask = codec::observe_png_artifact(bytes.mask.bytes(), budget, cancellation);
    if (!mask) {
        return std::unexpected(std::move(mask.error()));
    }
    if (mask->shape.width != protection.width || mask->shape.height != protection.height ||
        mask->shape.model != image::SampleModel::gray ||
        mask->shape.depth.bits() != image::byte_bits || mask->profile_embedded ||
        mask->resolution || !mask->mask_samples ||
        mask->protected_samples != protection.protected_samples) {
        return core::failure(core::ErrorCode::input,
                             "Bundle mask contradicts its recorded semantics");
    }
    return {};
}
core::Result<void> check_psf(const record::RunRecord& declared, const fs::BundleSnapshot& bytes,
                             core::Budget& budget, const core::Cancellation& cancellation) {
    // Borrow the same immutable encoded representation, with no byte copy or path reopen.
    auto decoded = codec::decode_psf_png(bytes.psf.bytes(), budget, cancellation);
    if (!decoded) {
        return std::unexpected(std::move(decoded.error()));
    }
    const auto shape = decoded->shape;
    if (!declared.execution.restoration) {
        return core::failure(core::ErrorCode::input, "Bundle restoration facts are absent");
    }
    const auto& facts = declared.execution.restoration->psf;
    if (facts.kind != methods::PsfKind::kernel || shape.width != facts.width ||
        shape.height != facts.height) {
        return core::failure(core::ErrorCode::input, "Bundle PSF contradicts its recorded shape");
    }
    // Same explicit bounded coefficient-vector exclusion as input PSF resolution, at most129²
    // doubles.
    std::vector<double> coefficients(std::size_t{shape.width} * shape.height);
    const auto sample_bytes = shape.depth.bytes();
    for (std::uint32_t y = 0; y < shape.height; ++y) {
        if (cancellation.requested(core::Checkpoint::verification)) {
            return core::cancelled();
        }
        const auto row = decoded->pixels.view().row(y);
        for (std::uint32_t x = 0; x < shape.width; ++x) {
            coefficients.at((std::size_t{y} * shape.width) + x) = image::read_sample(
                row.subspan(std::size_t{x} * sample_bytes, sample_bytes), shape.depth);
        }
    }
    const auto normalized = methods::normalize_psf(shape.width, shape.height, coefficients);
    if (!normalized) {
        return core::failure(core::ErrorCode::input,
                             "Bundle PSF has no finite positive coefficient sum");
    }
    const auto centroid = methods::psf_centroid(shape.width, shape.height, coefficients);
    double sum = 0;
    for (const auto coefficient : coefficients) {
        sum += coefficient;
    }
    if (!centroid || std::abs(sum - facts.normalized_sum) > methods::psf_normalization_tolerance ||
        std::abs(centroid->front() - facts.centroid_x) > methods::psf_normalization_tolerance ||
        std::abs(centroid->back() - facts.centroid_y) > methods::psf_normalization_tolerance) {
        return core::failure(core::ErrorCode::input, "Bundle PSF contradicts its recorded moments");
    }
    return {};
}
core::Result<void> artifacts(const record::RunRecord& declared, const fs::BundleSnapshot& bytes,
                             core::Budget& budget, const core::Cancellation& cancellation) {
    if (auto image = check_image(declared, bytes, budget, cancellation); !image) {
        return image;
    }
    for (const auto& asset : declared.assets) {
        auto checked =
            std::holds_alternative<record::ProtectionAsset>(asset)
                ? check_mask(std::get<record::ProtectionAsset>(asset), bytes, budget, cancellation)
                : check_psf(declared, bytes, budget, cancellation);
        if (!checked) {
            return checked;
        }
    }
    return {};
}
core::Result<void> validate_bundle(void* const state, const std::string& directory,
                                   const core::Cancellation& cancellation) {
    const auto& expected = *static_cast<ExpectedBundle*>(state);
    auto checked = verify_bundle(directory, cancellation);
    if (!checked) {
        auto error = std::move(checked.error());
        if (error.code == core::ErrorCode::input) {
            error.code = core::ErrorCode::output_verify;
        }
        return std::unexpected(std::move(error));
    }
    if (checked->declared.context.identity != expected.run ||
        checked->record.sha256 != expected.record.sha256 ||
        checked->record.bytes != expected.record.bytes) {
        return core::failure(core::ErrorCode::output_verify,
                             "Bundle identity disagrees with this run");
    }
    return {};
}
} // namespace
fs::BundleValidation bundle_validation(ExpectedBundle& expected) {
    return {.state = &expected, .validate = validate_bundle};
}
core::Result<VerifiedBundle> verify_bundle(const std::string& directory,
                                           const core::Cancellation& cancellation) {
    core::Budget budget{fs::bundle_snapshot_budget};
    auto bytes = fs::read_bundle(directory, budget, cancellation, record::record_max_bytes);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto declared = record::read_record(bytes->record.bytes());
    if (!declared) {
        return std::unexpected(declared.error());
    }
    auto agreed = record::agrees(*declared, bytes->contents.files, bytes->contents.directories);
    if (!agreed) {
        return std::unexpected(agreed.error());
    }
    auto checked = artifacts(*declared, *bytes, budget, cancellation);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    auto identity = fs::identify(bytes->record.bytes(), cancellation);
    if (!identity) {
        return std::unexpected(identity.error());
    }
    return VerifiedBundle{.declared = std::move(*declared), .record = std::move(*identity)};
}
} // namespace docenhance::host
