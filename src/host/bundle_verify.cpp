// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "bundle_verify.hpp"

#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/bundle.hpp"
#include "docenhance/io/digest.hpp"
#include "docenhance/methods/binarization.hpp"

#include <expected>
#include <utility>
#include <variant>
namespace docenhance::host {
namespace {
core::Result<void> artifacts(const bundle::DeclaredBundle& declared,
                             const io::BundleSnapshot& bytes, core::Budget& budget,
                             const core::Cancellation& cancellation) {
    if (!declared.operation) {
        return core::failure(core::ErrorCode::input, "Missing bundle operation");
    }
    auto image = io::observe_bundle_png(bytes.image.bytes(), budget, cancellation);
    if (!image) {
        return std::unexpected(image.error());
    }
    const auto profile =
        color::validate_output_profile(image->shape, image->profile.bytes(), budget, cancellation);
    if (!profile) {
        return std::unexpected(profile.error());
    }
    const auto& output = declared.output;
    const bool binary = std::holds_alternative<methods::Binarization>(*declared.operation);
    if (image->shape != output.shape || image->profile_embedded != output.profile_embedded ||
        image->resolution != output.resolution || (binary && !image->binary_samples)) {
        return core::failure(core::ErrorCode::input,
                             "Bundle image contradicts its recorded properties");
    }
    if (!declared.protection) {
        return {};
    }
    auto mask = io::observe_bundle_png(bytes.mask.bytes(), budget, cancellation);
    if (!mask) {
        return std::unexpected(mask.error());
    }
    const auto& protection = *declared.protection;
    if (mask->shape.width != protection.width || mask->shape.height != protection.height ||
        mask->shape.model != image::SampleModel::gray || mask->shape.depth != image::byte_bits ||
        mask->profile_embedded || mask->resolution || !mask->mask_samples ||
        mask->protected_samples != declared.illumination.protected_samples) {
        return core::failure(core::ErrorCode::input,
                             "Bundle mask contradicts its recorded semantics");
    }
    return {};
}
core::Result<void> prepare_bundle(void* const state, const std::string& directory,
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
    if (checked->declared.run != expected.run || checked->record.sha256 != expected.record.sha256 ||
        checked->record.bytes != expected.record.bytes) {
        return core::failure(core::ErrorCode::output_verify,
                             "Staged bundle identity disagrees with this run");
    }
    return {};
}
io::BundleObservation observe_bundle(void* const state, const std::string& directory) {
    const auto& expected = *static_cast<ExpectedBundle*>(state);
    core::Budget budget{bundle::record_max_bytes};
    auto bytes = io::read_bundle_record(directory, bundle::record_max_bytes, budget);
    if (!bytes) {
        return io::BundleObservation::unobservable;
    }
    const auto identity = io::identify(bytes->bytes());
    if (!identity) {
        return io::BundleObservation::unobservable;
    }
    if (identity->sha256 != expected.record.sha256 || identity->bytes != expected.record.bytes) {
        return io::BundleObservation::other_or_absent;
    }
    // Only the digest is needed now; release this snapshot before the full validation ledger.
    *bytes = {};
    const auto checked = verify_bundle(directory, {});
    if (!checked) {
        return checked.error().code == core::ErrorCode::input
                   ? io::BundleObservation::integrity_failure
                   : io::BundleObservation::unobservable;
    }
    if (checked->record.sha256 != expected.record.sha256 || checked->declared.run != expected.run) {
        return io::BundleObservation::unobservable;
    }
    return io::BundleObservation::consistent;
}
} // namespace
io::BundleValidation bundle_validation(ExpectedBundle& expected) {
    return {.state = &expected, .prepare = prepare_bundle, .observe = observe_bundle};
}
core::Result<VerifiedBundle> verify_bundle(const std::string& directory,
                                           const core::Cancellation& cancellation) {
    core::Budget budget{io::bundle_snapshot_budget};
    auto bytes = io::read_bundle(directory, budget, cancellation, bundle::record_max_bytes);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto declared = bundle::read_record(bytes->record.bytes());
    if (!declared) {
        return std::unexpected(declared.error());
    }
    auto agreed = bundle::agrees(*declared, bytes->contents.files, bytes->contents.directories);
    if (!agreed) {
        return std::unexpected(agreed.error());
    }
    auto checked = artifacts(*declared, *bytes, budget, cancellation);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    auto identity = io::identify(bytes->record.bytes(), cancellation);
    if (!identity) {
        return std::unexpected(identity.error());
    }
    return VerifiedBundle{.declared = std::move(*declared), .record = std::move(*identity)};
}
} // namespace docenhance::host
