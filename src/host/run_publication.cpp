// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "run_publication.hpp"

#include "bundle_verify.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/codec/png_artifact.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/fs/publication.hpp"
#include "docenhance/pipeline/render.hpp"
#include "docenhance/record/inventory.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/source.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
namespace docenhance::host {
namespace {
core::Result<std::optional<record::ProtectionAsset>> protection(const RunPublication& run) {
    if (!run.rendering.get().mask) {
        return std::nullopt;
    }
    const auto& encoded = *run.rendering.get().mask;
    const auto cancellation = run.scheduler.get().cancellation();
    auto observed = codec::observe_png_artifact(encoded.bytes(), run.budget.get(), cancellation);
    if (!observed) {
        auto error = std::move(observed.error());
        if (error.code == core::ErrorCode::input) {
            error.code = core::ErrorCode::output_verify;
        }
        return std::unexpected(std::move(error));
    }
    auto identity = fs::identify(encoded.bytes(), cancellation);
    if (!identity) {
        return std::unexpected(std::move(identity.error()));
    }
    return record::ProtectionAsset{
        .sha256 = std::move(identity->sha256),
        .bytes = identity->bytes,
        .width = observed->shape.width,
        .height = observed->shape.height,
        .protected_samples = observed->protected_samples,
    };
}
core::Result<record::RunRecord> make_record(RunPublication& run, core::ContentIdentity output,
                                            std::optional<record::ProtectionAsset> mask) {
    if (!run.rendering.get().output.decoded_and_compared) {
        return core::failure(core::ErrorCode::invariant,
                             "Rendering did not compare its produced PNG");
    }
    auto source = record::source_container(run.rendering.get().source);
    if (!source) {
        return std::unexpected(std::move(source.error()));
    }
    auto request = record::requested(run.request.get().plan(), run.mask);
    if (!request) {
        return std::unexpected(std::move(request.error()));
    }
    const auto build = core::build_facts();
    const auto& actual = run.rendering.get().output;
    record::RunRecord result{
        .context = std::move(run.context),
        .build =
            {
                .version = std::string{build.version},
                .platform = std::string{build.platform},
                .compiler = std::string{build.compiler},
                .dependency_lock_sha256 = std::string{build.dependency_lock_sha256},
            },
        .source =
            {
                .name = std::move(run.source_name),
                .sha256 = std::move(run.source.sha256),
                .bytes = run.source.bytes,
                .container = std::move(*source),
            },
        .request = std::move(*request),
        .execution = record::Execution(),
        .assets = std::vector<record::Asset>(),
        .output =
            {
                .sha256 = std::move(output.sha256),
                .bytes = output.bytes,
                .width = actual.shape.width,
                .height = actual.shape.height,
                .channels = actual.shape.model,
                .depth = actual.shape.depth,
                .profile_embedded = actual.profile_embedded,
                .resolution = actual.resolution,
            },
    };
    result.execution.interpretation = run.rendering.get().interpretation;
    static_cast<methods::StageReports&>(result.execution) = run.rendering.get().stages;
    if (mask) {
        result.assets.emplace_back(std::move(*mask));
    }
    if (run.rendering.get().psf) {
        const auto& identity = run.rendering.get().psf->identity;
        result.assets.emplace_back(
            record::PsfAsset{.sha256 = identity.sha256, .bytes = identity.bytes});
    }
    return result;
}
std::size_t
prepare_bundle_files(const RunPublication& run, std::span<const std::byte> record_bytes,
                     std::array<fs::BundleFile, record::record_max_artifacts + 1>& files) {
    std::size_t count = 0;
    files.at(count++) = {
        .relative = record::image_name,
        .bytes = run.rendering.get().result.bytes(),
    };
    if (run.rendering.get().mask) {
        files.at(count++) = {
            .relative = record::mask_name,
            .bytes = run.rendering.get().mask->bytes(),
        };
    }
    if (run.rendering.get().psf) {
        files.at(count++) = {
            .relative = record::psf_name,
            .bytes = run.rendering.get().psf->encoded.bytes(),
        };
    }
    files.at(count++) = {
        .relative = record::record_name,
        .bytes = record_bytes,
    };
    return count;
}
} // namespace
app::ProcessResult publish_run(RunPublication& run, app::ProcessFailure& failure) {
    static_assert(std::is_nothrow_move_constructible_v<app::Published>);
    static_assert(std::is_nothrow_move_constructible_v<app::ProcessFailure>);
    static_assert(std::is_nothrow_constructible_v<app::ProcessResult, app::Published&&>);
    const auto refuse = [&](core::Error error) -> app::ProcessResult {
        failure.error = std::move(error);
        if (failure.diagnostics) {
            failure.diagnostics->charged_peak_bytes = run.budget.get().peak();
        }
        return std::unexpected(std::move(failure));
    };
    auto mask = protection(run);
    if (!mask) {
        return refuse(std::move(mask.error()));
    }
    const auto cancellation = run.scheduler.get().cancellation();
    auto image_identity = fs::identify(run.rendering.get().result.bytes(), cancellation);
    if (!image_identity) {
        return refuse(std::move(image_identity.error()));
    }
    auto run_record = make_record(run, std::move(*image_identity), std::move(*mask));
    if (!run_record) {
        return refuse(std::move(run_record.error()));
    }
    auto serialized = record::serialize(*run_record);
    if (!serialized) {
        return refuse(std::move(serialized.error()));
    }
    const auto record_bytes = std::as_bytes(std::span{*serialized});
    auto identity = fs::identify(record_bytes, cancellation);
    if (!identity) {
        return refuse(std::move(identity.error()));
    }
    // All strings, identities and typed record storage exist before entering native publication.
    ExpectedBundle expected{
        .run = run_record->context.identity,
        .record = *identity,
    };
    constexpr auto mib = std::uint64_t{1024} * 1024;
    app::Published success{
        .output = std::string(),
        .record_identity = std::move(*identity),
        .record = std::move(*run_record),
        .diagnostics =
            {
                .threads = run.scheduler.get().workers(),
                .memory_limit_mib = run.budget.get().limit() / mib,
                .charged_peak_bytes = run.budget.get().peak(),
            },
    };
    std::array<fs::BundleFile, record::record_max_artifacts + 1> files{};
    const auto count = prepare_bundle_files(run, record_bytes, files);
    run.publication_started.get() = true;
    auto published =
        fs::publish_bundle(run.request.get().output_directory(), std::span{files}.first(count),
                           cancellation, bundle_validation(expected));
    if (!published) {
        failure.error = std::move(published.error().error);
        failure.publication = published.error().publication;
        if (failure.diagnostics) {
            failure.diagnostics->charged_peak_bytes = run.budget.get().peak();
        }
        return std::unexpected(std::move(failure));
    }
    success.output = std::move(*published);
    success.diagnostics.charged_peak_bytes = run.budget.get().peak();
    return success;
}
} // namespace docenhance::host
