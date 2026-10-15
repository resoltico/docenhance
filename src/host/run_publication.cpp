// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "run_publication.hpp"

#include "bundle_verify.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/fs/png.hpp"
#include "docenhance/fs/publication.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/stages.hpp"
#include "docenhance/record/record.hpp"

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::host {
static_assert(std::is_nothrow_move_constructible_v<PublishedRun>);
static_assert(std::is_nothrow_move_constructible_v<core::Error>);
namespace {
// What the writers fill in as they run. The record writer is declared last, so by the time it
// runs every artifact it describes has been written, verified and identified.
struct Composition {
    explicit Composition(const RunPublication& publication) noexcept : run(publication) {}
    std::reference_wrapper<const RunPublication> run;
    std::optional<image::ConversionReport> conversion;
    record::OutputFacts output{};
    std::optional<record::ProtectionFacts> protection;
    ExpectedBundle expected;
};

core::Result<void> write_image(void* const state, const fs::BundleSlot& slot) {
    auto& composed = *static_cast<Composition*>(state);
    const auto& run = composed.run.get();
    image::OutputDescriptor descriptor{};
    auto written = std::visit(
        [&](const auto& artwork) {
            using Kind = std::decay_t<decltype(artwork)>;
            if constexpr (std::is_same_v<Kind, BinaryArtwork>) {
                const image::PlaneRows rows{artwork.samples};
                descriptor = rows.descriptor();
                return fs::write_verified_png(slot, artwork.samples, run.budget.get(),
                                              run.cancellation.get());
            } else {
                descriptor = artwork.rows.get().descriptor();
                return fs::write_verified_png_rows(slot, artwork.rows.get(), run.budget.get(),
                                                   run.cancellation.get());
            }
        },
        run.artwork);
    if (!written) {
        return written;
    }
    auto identity = fs::identify_slot(slot, core::bundle_max_file_bytes, run.cancellation.get());
    if (!identity) {
        return std::unexpected(identity.error());
    }
    if (const auto* const artwork = std::get_if<ContinuousArtwork>(&run.artwork)) {
        composed.conversion = artwork->converter.get().report();
        composed.conversion->verified = true;
    }
    composed.output = {
        .artifact = {.name = record::image_name, .identity = std::move(*identity)},
        .shape = descriptor.shape,
        .profile_embedded = !descriptor.profile.empty(),
        .resolution = descriptor.resolution,
        // The writer above compared the encoded file against these samples and this metadata;
        // that comparison is the only thing that puts this value here.
        .verification = record::Verification::decoded_and_compared,
    };
    return {};
}

core::Result<void> write_mask(void* const state, const fs::BundleSlot& slot) {
    auto& composed = *static_cast<Composition*>(state);
    const auto& run = composed.run.get();
    const auto* const artwork = std::get_if<ContinuousArtwork>(&run.artwork);
    if (artwork == nullptr || !artwork->mask) {
        return core::failure(core::ErrorCode::invariant, "A mask was declared but not supplied");
    }
    const auto canonical = artwork->mask->canonical;
    auto written =
        fs::write_verified_png(slot, canonical, run.budget.get(), run.cancellation.get());
    if (!written) {
        return written;
    }
    auto identity = fs::identify_slot(slot, core::bundle_max_file_bytes, run.cancellation.get());
    if (!identity) {
        return std::unexpected(identity.error());
    }
    composed.protection = record::ProtectionFacts{
        .original = artwork->mask->supplied,
        .stored = {.name = record::mask_name, .identity = std::move(*identity)},
        .width = canonical.width(),
        .height = canonical.height(),
    };
    return {};
}

core::Result<void> write_record(void* const state, const fs::BundleSlot& slot) {
    auto& composed = *static_cast<Composition*>(state);
    const auto& run = composed.run.get();
    const auto operation = std::visit(
        [](const auto& artwork) -> record::Operation {
            using Kind = std::decay_t<decltype(artwork)>;
            if constexpr (std::is_same_v<Kind, BinaryArtwork>) {
                return artwork.method;
            } else {
                return artwork.operation;
            }
        },
        run.artwork);
    record::RunRecord record{
        .context = run.context.get(),
        .build = core::build_facts(),
        .source =
            {
                .identity = run.source,
                .name = run.source_name,
                .decoding = run.source_decoding,
            },
        .operation = operation,
        .protection_supplied = composed.protection.has_value(),
        .output = composed.output,
        .protection = composed.protection,
        .conversion = composed.conversion,
        .stages =
            {
                .denoising = {.progress = methods::StageProgress::completed},
                .restoration = {.progress = methods::StageProgress::completed},
                .contrast = {.progress = methods::StageProgress::completed},
                .sharpening = {.progress = methods::StageProgress::completed},
            },
        .rotation = run.rotation,
    };
    std::visit(
        [&record](const auto& artwork) {
            using Kind = std::decay_t<decltype(artwork)>;
            if constexpr (std::is_same_v<Kind, BinaryArtwork>) {
                record.otsu = artwork.otsu;
            } else {
                record.stages = artwork.stages.get();
            }
        },
        run.artwork);
    auto written = record::serialize(record);
    if (!written) {
        return std::unexpected(written.error());
    }
    // The record's own digest is taken from these bytes and reported in the response. Embedding it
    // would mean hashing bytes that contain the hash.
    const std::span<const char> text{written->data(), written->size()};
    auto identity = fs::identify(std::as_bytes(text), run.cancellation.get());
    if (!identity) {
        return std::unexpected(identity.error());
    }
    auto placed = fs::write_bytes(slot, *written, run.cancellation.get());
    if (!placed) {
        return placed;
    }
    composed.expected.record = std::move(*identity);
    composed.expected.run = record.context.identity;
    return {};
}
} // namespace

core::Result<app::PublishedContinuous> published_continuous(PublishedRun published,
                                                            image::SourceFacts source,
                                                            methods::StageObservations& reports,
                                                            image::AlphaPolicy alpha) {
    // The conversion the record states, so the response and the record cannot disagree.
    if (!published.conversion) {
        return std::unexpected(core::Error{
            .code = core::ErrorCode::output_verify,
            .message = "Published continuous record has no conversion observations",
            .publication = core::Publication::completed,
        });
    }
    auto report = *published.conversion;
    // Derived from the comparison that ran, not asserted because publication returned.
    report.verified = published.verification == record::Verification::decoded_and_compared;
    return app::PublishedContinuous{
        .output = std::move(published.output),
        .alpha = alpha,
        .conversion = report,
        .stages = std::move(reports),
        .run = std::move(published.run),
        .record = std::move(published.record),
        .source_decoding = source,

    };
}
core::Result<PublishedRun> publish_run(const RunPublication& run) {
    Composition composed{run};
    std::vector<fs::BundleFile> files;
    files.push_back({.relative = record::image_name, .state = &composed, .write = write_image});
    const auto* const artwork = std::get_if<ContinuousArtwork>(&run.artwork);
    if (artwork != nullptr && artwork->mask) {
        files.push_back({.relative = record::mask_name, .state = &composed, .write = write_mask});
    }
    files.push_back({.relative = record::record_name, .state = &composed, .write = write_record});
    PublishedRun result{
        .output = {},
        .run = run.context.get().identity,
        .record = {.sha256 = {}, .bytes = 0},
        .verification = record::Verification::decoded_and_compared,
        .conversion = std::nullopt,
    };
    auto published = fs::publish_bundle(run.output_directory.get(), files, run.cancellation.get(),
                                        bundle_validation(composed.expected));
    if (!published) {
        return std::unexpected(std::move(published.error()));
    }
    result.output = std::move(*published);
    result.record = std::move(composed.expected.record);
    result.verification = composed.output.verification;
    result.conversion = composed.conversion;
    return result;
}
} // namespace docenhance::host
