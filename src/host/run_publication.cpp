// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "run_publication.hpp"

#include "bundle_verify.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/digest.hpp"
#include "docenhance/io/png.hpp"
#include "docenhance/io/publication.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/sharpening.hpp"

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
    bundle::OutputFacts output{};
    std::optional<bundle::ProtectionFacts> protection;
    ExpectedBundle expected;
};

core::Result<void> write_image(void* const state, const io::BundleSlot& slot) {
    auto& composed = *static_cast<Composition*>(state);
    const auto& run = composed.run.get();
    image::OutputDescriptor descriptor{};
    auto written = std::visit(
        [&](const auto& artwork) {
            using Kind = std::decay_t<decltype(artwork)>;
            if constexpr (std::is_same_v<Kind, BinaryArtwork>) {
                const image::PlaneRows rows{artwork.samples};
                descriptor = rows.descriptor();
                return io::write_verified_png(slot, artwork.samples, run.budget.get(),
                                              run.cancellation.get());
            } else {
                descriptor = artwork.rows.get().descriptor();
                return io::write_verified_png_rows(slot, artwork.rows.get(), run.budget.get(),
                                                   run.cancellation.get());
            }
        },
        run.artwork);
    if (!written) {
        return written;
    }
    auto identity = io::identify_slot(slot, core::bundle_max_file_bytes, run.cancellation.get());
    if (!identity) {
        return std::unexpected(identity.error());
    }
    if (const auto* const artwork = std::get_if<ContinuousArtwork>(&run.artwork)) {
        composed.conversion = artwork->converter.get().report();
        composed.conversion->verified = true;
    }
    composed.output = {
        .artifact = {.name = bundle::image_name, .identity = std::move(*identity)},
        .shape = descriptor.shape,
        .profile_embedded = !descriptor.profile.empty(),
        .resolution = descriptor.resolution,
        // The writer above compared the encoded file against these samples and this metadata;
        // that comparison is the only thing that puts this value here.
        .verification = bundle::Verification::decoded_and_compared,
    };
    return {};
}

core::Result<void> write_mask(void* const state, const io::BundleSlot& slot) {
    auto& composed = *static_cast<Composition*>(state);
    const auto& run = composed.run.get();
    const auto* const artwork = std::get_if<ContinuousArtwork>(&run.artwork);
    if (artwork == nullptr || !artwork->mask) {
        return core::failure(core::ErrorCode::invariant, "A mask was declared but not supplied");
    }
    const auto canonical = artwork->mask->canonical;
    auto written =
        io::write_verified_png(slot, canonical, run.budget.get(), run.cancellation.get());
    if (!written) {
        return written;
    }
    auto identity = io::identify_slot(slot, core::bundle_max_file_bytes, run.cancellation.get());
    if (!identity) {
        return std::unexpected(identity.error());
    }
    composed.protection = bundle::ProtectionFacts{
        .original = artwork->mask->supplied,
        .stored = {.name = bundle::mask_name, .identity = std::move(*identity)},
        .width = canonical.width(),
        .height = canonical.height(),
    };
    return {};
}

core::Result<void> write_record(void* const state, const io::BundleSlot& slot) {
    auto& composed = *static_cast<Composition*>(state);
    const auto& run = composed.run.get();
    const auto operation = std::visit(
        [](const auto& artwork) -> bundle::Operation {
            using Kind = std::decay_t<decltype(artwork)>;
            if constexpr (std::is_same_v<Kind, BinaryArtwork>) {
                return artwork.method;
            } else {
                return artwork.operation;
            }
        },
        run.artwork);
    const methods::IlluminationReport none;
    const methods::DenoisingReport disabled{.complete = true};
    const methods::ContrastReport disabled_contrast{.complete = true};
    const methods::SharpenReport disabled_sharpen{.complete = true};
    bundle::RunRecord record{
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
        .illumination = none,
        .denoising = disabled,
        .contrast = disabled_contrast,
        .sharpening = disabled_sharpen,
    };
    std::visit(
        [&record](const auto& artwork) {
            using Kind = std::decay_t<decltype(artwork)>;
            if constexpr (std::is_same_v<Kind, BinaryArtwork>) {
                record.otsu = artwork.otsu;
            } else {
                record.illumination = artwork.illumination.get();
                record.denoising = artwork.denoising.get();
                record.contrast = artwork.contrast.get();
                record.sharpening = artwork.sharpening.get();
            }
        },
        run.artwork);
    auto written = bundle::serialize(record);
    if (!written) {
        return std::unexpected(written.error());
    }
    // The record's own digest is taken from these bytes and reported in the response. Embedding it
    // would mean hashing bytes that contain the hash.
    const std::span<const char> text{written->data(), written->size()};
    auto identity = io::identify(std::as_bytes(text), run.cancellation.get());
    if (!identity) {
        return std::unexpected(identity.error());
    }
    auto placed = io::write_bytes(slot, *written, run.cancellation.get());
    if (!placed) {
        return placed;
    }
    composed.expected.record = std::move(*identity);
    composed.expected.run = record.context.identity;
    return {};
}
} // namespace

core::Result<PublishedRun> publish_run(const RunPublication& run) {
    Composition composed{run};
    std::vector<io::BundleFile> files;
    files.push_back({.relative = bundle::image_name, .state = &composed, .write = write_image});
    const auto* const artwork = std::get_if<ContinuousArtwork>(&run.artwork);
    if (artwork != nullptr && artwork->mask) {
        files.push_back({.relative = bundle::mask_name, .state = &composed, .write = write_mask});
    }
    files.push_back({.relative = bundle::record_name, .state = &composed, .write = write_record});
    PublishedRun result{
        .output = {},
        .run = run.context.get().identity,
        .record = {.sha256 = {}, .bytes = 0},
        .verification = bundle::Verification::decoded_and_compared,
        .conversion = std::nullopt,
    };
    auto published = io::publish_bundle(run.output_directory.get(), files, run.cancellation.get(),
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
