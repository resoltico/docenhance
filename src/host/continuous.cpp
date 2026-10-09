// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "continuous.hpp"

#include "contrast.hpp"
#include "denoising.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/paths.hpp"
#include "docenhance/io/source.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/surface.hpp"
#include "linear_rows.hpp"
#include "protection.hpp"
#include "restoration.hpp"
#include "run_publication.hpp"
#include "sharpening.hpp"
#include "stage_reports.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <utility>
#include <variant>
namespace docenhance::host {
namespace {
struct ContinuousRun {
    std::reference_wrapper<const app::ProcessRequest> request;
    std::reference_wrapper<color::Converter> converter;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
    ContinuousReports reports;
    std::reference_wrapper<const bundle::RunContext> context;
};
core::Result<void> disabled_observations(ContinuousRun run,
                                         image::PlaneView<const std::uint8_t> protection) {
    run.reports.illumination.get() = {};
    if (!protection.empty()) {
        for (std::uint32_t y = 0; y < protection.height(); ++y) {
            if (run.cancellation.get().requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            run.reports.illumination.get().protected_samples += static_cast<std::uint64_t>(
                std::ranges::count_if(protection.row(y), [](auto p) { return p != 0; }));
        }
    }
    const auto extent = run.converter.get().extent();
    run.reports.illumination.get().eligible_samples =
        (std::uint64_t{extent.width} * extent.height) -
        run.reports.illumination.get().protected_samples;
    run.reports.illumination.get().complete = true;
    return {};
}
// Prepares the rows the bundle will contain: either the converter itself, or the converter seen
// through the illumination model. Publication happens once, afterwards, for the whole bundle.
// The model outlives the rows that reference it, so both belong to the caller: the rows are read
// during publication, long after this function returns.
core::Result<std::optional<IlluminationModel>>
prepare(ContinuousRun run, image::PlaneView<const std::uint8_t> protection) {
    const auto* const surface = std::get_if<methods::Surface>(&run.request.get().illumination());
    if (const auto* const morphology =
            std::get_if<methods::Morphology>(&run.request.get().illumination())) {
        auto model = methods::MorphologyModel::prepare(
            {run.converter.get(), protection}, *morphology, run.budget.get(),
            run.cancellation.get(), run.reports.illumination.get());
        if (!model) {
            return std::unexpected(model.error());
        }
        return model->active() ? std::optional<IlluminationModel>{std::move(*model)} : std::nullopt;
    }
    if (surface == nullptr) {
        auto observed = disabled_observations(run, protection);
        if (!observed) {
            return std::unexpected(observed.error());
        }
        return std::nullopt;
    }
    auto model = methods::SurfaceModel::prepare({run.converter.get(), protection}, *surface,
                                                run.budget.get(), run.cancellation.get(),
                                                run.reports.illumination.get());
    if (!model) {
        return std::unexpected(model.error());
    }
    return model->active() ? std::optional<IlluminationModel>{std::move(*model)} : std::nullopt;
}
core::Result<app::PublishedContinuous> publish_frame(image::LinearSource& source, bool prepared,
                                                     const ContinuousRun& run,
                                                     const Protection& protection,
                                                     const io::IdentifiedRaster& decoded) {
    auto& budget = run.budget.get();
    const auto& cancellation = run.cancellation.get();
    const auto& request = run.request.get();
    const auto& context = run.context.get();
    const auto& illumination = run.reports.illumination.get();
    const auto& denoising = run.reports.denoising.get();
    const auto& contrast = run.reports.contrast.get();
    const auto& sharpening = run.reports.sharpening.get();
    const auto& operation = std::get<image::Continuous>(request.operation());
    auto block =
        image::Plane<double>::allocate(budget, image::linear_block_pixels * image::rgb_channels, 1);
    if (!block) {
        return std::unexpected(block.error());
    }
    ContinuousRows rows{source, run.converter.get().descriptor(), std::move(*block), prepared};
    auto published = publish_run({
        .output_directory = request.output_directory(),
        .artwork =
            ContinuousArtwork{
                .rows = rows,
                .operation = operation,
                .converter = std::cref(run.converter.get()),
                .mask = protection.facts,
                .illumination = illumination,
                .denoising = denoising,
                .contrast = contrast,
                .sharpening = sharpening,
                .restoration = run.reports.restoration.get(),
            },
        .budget = budget,
        .cancellation = cancellation,
        .context = context,
        .source = decoded.source,
        .source_name = io::file_name(request.input()),
        .source_decoding = decoded.description,
        .rotation = request.rotation(),

    });
    if (!published) {
        return std::unexpected(std::move(published.error()));
    }
    return published_continuous(std::move(*published), decoded.description, run.reports);
}
core::Result<app::PublishedContinuous> finish_frame(image::LinearSource& source,
                                                    bool source_prepared, const ContinuousRun& run,
                                                    const Protection& protection,
                                                    const io::IdentifiedRaster& decoded) {
    auto& contrast = run.reports.contrast.get();
    auto& budget = run.budget.get();
    const auto& cancellation = run.cancellation.get();
    contrast.eligible_samples = run.reports.denoising.get().eligible_samples;
    contrast.protected_samples = run.reports.denoising.get().protected_samples;
    const ContrastInput contrast_input{
        .source = source,
        .mask = protection.mask.view(),
        .prepared = source_prepared,
    };
    auto mapping = prepare_contrast(contrast_input, run.request.get().contrast(), budget,
                                    cancellation, contrast);
    if (!mapping) {
        return std::unexpected(mapping.error());
    }
    ContrastedSource contrasted{contrast_input, *mapping ? &**mapping : nullptr, contrast,
                                cancellation};
    run.reports.sharpening.get().eligible_samples = contrast.eligible_samples;
    run.reports.sharpening.get().protected_samples = contrast.protected_samples;
    const SharpenInput sharpen_input{
        .source = contrasted,
        .mask = protection.mask.view(),
        .prepared = contrasted.prepared(),
    };
    auto sharpening_model = prepare_sharpening(sharpen_input, run.request.get().sharpening(),
                                               budget, cancellation, run.reports.sharpening.get());
    if (!sharpening_model) {
        return std::unexpected(sharpening_model.error());
    }
    SharpenedSource sharpened{sharpen_input, *sharpening_model ? &**sharpening_model : nullptr,
                              run.reports.sharpening.get(), cancellation};
    return publish_frame(sharpened, sharpened.prepared(), run, protection, decoded);
}
core::Result<app::PublishedContinuous>
restore_and_finish(image::LinearSource& source, bool source_prepared, const ContinuousRun& run,
                   const Protection& protection, const io::IdentifiedRaster& decoded) {
    auto& budget = run.budget.get();
    const auto& cancellation = run.cancellation.get();
    auto& restoration = run.reports.restoration.get();
    restoration.eligible_samples = run.reports.denoising.get().eligible_samples;
    restoration.protected_samples = run.reports.denoising.get().protected_samples;
    const RestoreInput restoration_input{
        .source = source,
        .mask = protection.mask.view(),
        .prepared = source_prepared,
        .geometry_transformed =
            run.converter.get().report().orientation != image::Orientation::normal() ||
            run.request.get().rotation().degrees() != 0,
        .illumination = run.reports.illumination.get(),
        .denoising = run.reports.denoising.get(),
    };
    auto restored_model = prepare_restoration(restoration_input, run.request.get().restoration(),
                                              budget, cancellation, restoration);
    if (!restored_model) {
        return std::unexpected(restored_model.error());
    }
    RestoredSource restored{restoration_input, *restored_model ? &**restored_model : nullptr,
                            restoration, cancellation};
    return finish_frame(restored, restored.prepared(), run, protection, decoded);
}
} // namespace
core::Result<app::PublishedContinuous> continuous(const app::ProcessRequest& request,
                                                  image::Continuous operation,
                                                  ContinuousExecution execution,
                                                  ContinuousReports reports) {
    auto& illumination = reports.illumination.get();
    auto& denoising = reports.denoising.get();
    const auto& cancellation = execution.cancellation.get();
    core::Budget budget{core::continuous_processing_budget};
    initialize_stages(request, reports);
    auto decoded =
        io::load_source(request.input(), budget, operation.parameters().profile, cancellation);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    auto converter = color::Converter::create(decoded->raster, operation, budget, cancellation,
                                              request.rotation());
    if (!converter) {
        return std::unexpected(converter.error());
    }
    auto protection = load_protection(request, **converter, budget, cancellation);
    if (!protection) {
        return std::unexpected(protection.error());
    }
    const ContinuousRun run{
        .request = request,
        .converter = **converter,
        .budget = budget,
        .cancellation = cancellation,
        .reports = reports,
        .context = execution.context,
    };
    auto prepared = prepare(run, protection->mask.view().as_const());
    if (!prepared) {
        return std::unexpected(prepared.error());
    }
    denoising.eligible_samples = illumination.eligible_samples;
    denoising.protected_samples = illumination.protected_samples;
    IlluminatedSource entering{**converter, *prepared ? &**prepared : nullptr,
                               protection->mask.view().as_const(), illumination, cancellation};
    auto planes = prepare_denoising({entering, protection->mask.view().as_const()}, request, budget,
                                    cancellation, denoising);
    if (!planes) {
        return std::unexpected(planes.error());
    }
    DenoisedSource final_source{entering, *planes, protection->mask.view().as_const(), denoising,
                                cancellation};
    if (active_denoising(*planes)) {
        auto assessed = assess_denoising(final_source, budget, cancellation, denoising);
        if (!assessed) {
            return std::unexpected(assessed.error());
        }
    }
    return restore_and_finish(final_source, active_denoising(*planes), run, *protection, *decoded);
}
} // namespace docenhance::host
