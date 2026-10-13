// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "restoration.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/psf_png.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/opencv/restoration.hpp"
#include "linear_rows.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>
namespace docenhance::host {
namespace {
void preceding_warning(RestoreInput input, methods::RestorationReport& report) {
    if (report.requested) {
        report.after_transform_warning = input.geometry_transformed ||
                                         input.illumination.get().changed_samples != 0 ||
                                         input.denoising.get().changed_samples != 0;
    }
}
core::Result<methods::ResolvedPsf> load_kernel(const methods::FilePsf& file, core::Budget& budget,
                                               const core::Cancellation& cancellation) {
    auto loaded = io::load_psf_png(file.path, budget, cancellation);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    const auto& shape = loaded->raster.shape;
    methods::ResolvedPsf result{
        .width = shape.width,
        .height = shape.height,
        .coefficients = std::vector<double>(std::size_t{shape.width} * shape.height),
        .source_identity = methods::PsfIdentity{.path = file.path, .identity = loaded->source},
    };
    const auto bytes = shape.depth.bytes();
    for (std::uint32_t y = 0; y < shape.height; ++y) {
        if (cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        const auto row = loaded->raster.pixels.view().row(y);
        for (std::uint32_t x = 0; x < shape.width; ++x) {
            result.coefficients.at((std::size_t{y} * shape.width) + x) =
                image::read_sample(row.subspan(std::size_t{x} * bytes, bytes), shape.depth);
        }
    }
    auto const normalized = methods::normalize_psf(shape.width, shape.height, result.coefficients);
    if (!normalized) {
        return core::failure(core::ErrorCode::input,
                             "PSF coefficients require a finite positive sum");
    }
    auto centroid = methods::psf_centroid(shape.width, shape.height, result.coefficients);
    if (!centroid) {
        return std::unexpected(centroid.error());
    }
    result.centroid_x = centroid->at(0);
    result.centroid_y = centroid->at(1);
    return result;
}
} // namespace
core::Result<std::optional<opencv::RestorationModel>>
prepare_restoration(RestoreInput input, const methods::Restoration& method, core::Budget& budget,
                    const core::Cancellation& cancellation, methods::RestorationReport& report) {
    if (std::holds_alternative<methods::RestorationOff>(method)) {
        report.progress = methods::StageProgress::completed;
        return std::nullopt;
    }
    const auto& selected = std::get<methods::Wiener>(method);
    preceding_warning(input, report);
    const auto* const file = std::get_if<methods::FilePsf>(&selected.parameters().psf);
    auto psf =
        file != nullptr ? load_kernel(*file, budget, cancellation) : opencv::resolve_psf(selected);
    if (!psf) {
        return std::unexpected(psf.error());
    }
    preceding_warning(input, report);
    auto model = opencv::RestorationModel::prepare(
        input.source.get(), input.mask, selected, std::move(*psf),
        {budget, cancellation, report,
         input.prefix_observed ? image::RowUse::verification : image::RowUse::output});
    preceding_warning(input, report);
    if (!model) {
        return std::unexpected(model.error());
    }
    RestoredSource assessed{input, &*model, report, cancellation};
    if (assessed.active()) {
        auto result = assess_restoration(assessed, budget, cancellation, report);
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    return std::optional<opencv::RestorationModel>{std::move(*model)};
}
core::Result<void> RestoredSource::read(image::RowRange range, std::span<double> rgb,
                                        image::RowUse use) {
    if (use == image::RowUse::output && active()) {
        report_.get().progress = methods::StageProgress::evaluating;
    }
    auto read =
        input_.source.get().read(range, rgb, prefix_observed() ? image::RowUse::verification : use);
    if (!read) {
        return read;
    }
    if (use == image::RowUse::output) {
        preceding_warning(input_, report_.get());
    }
    if (!active()) {
        return read;
    }
    methods::RestorationReport ignored;
    auto applied =
        model_->apply(range, rgb, input_.mask,
                      use == image::RowUse::output ? report_.get() : ignored, cancellation_);
    if (!applied && use == image::RowUse::output) {
        report_.get().status = methods::RestorationStatus::failed;
        report_.get().reason = methods::RestorationReason::processing_failure;
    }
    return applied;
}
core::Result<void> assess_restoration(RestoredSource& source, core::Budget& budget,
                                      const core::Cancellation& cancellation,
                                      methods::RestorationReport& report) {
    auto observed =
        observe_rows(source, budget, cancellation, true, &report.preparation_charge_peak);
    if (!observed) {
        if (report.progress == methods::StageProgress::prepared) {
            report.status = methods::RestorationStatus::failed;
            report.reason = methods::RestorationReason::processing_failure;
        }
        return observed;
    }
    report.status = report.changed_samples == 0 ? methods::RestorationStatus::no_change
                                                : methods::RestorationStatus::applied;
    report.reason = report.changed_samples == 0 ? methods::RestorationReason::no_effect
                                                : methods::RestorationReason::none;
    report.progress = methods::StageProgress::completed;
    return {};
}
} // namespace docenhance::host
