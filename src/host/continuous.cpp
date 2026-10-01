// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "continuous.hpp"

#include "bundle.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/bundle.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/protection_png.hpp"
#include "docenhance/io/source.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "illumination_rows.hpp"

#include <algorithm>
#include <cstddef>
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
    std::reference_wrapper<methods::IlluminationReport> report;
};
core::Result<void> disabled_observations(ContinuousRun run,
                                         image::PlaneView<const std::uint8_t> protection) {
    run.report.get() = {};
    if (!protection.empty()) {
        for (std::uint32_t y = 0; y < protection.height(); ++y) {
            if (run.cancellation.get().requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            run.report.get().protected_samples += static_cast<std::uint64_t>(
                std::ranges::count_if(protection.row(y), [](auto p) { return p != 0; }));
        }
    }
    const auto extent = run.converter.get().extent();
    run.report.get().eligible_samples =
        (std::uint64_t{extent.width} * extent.height) - run.report.get().protected_samples;
    run.report.get().complete = true;
    return {};
}
// Prepares the rows the bundle will contain: either the converter itself, or the converter seen
// through the illumination model. Publication happens once, afterwards, for the whole bundle.
// The model outlives the rows that reference it, so both belong to the caller: the rows are read
// during publication, long after this function returns.
struct Prepared {
    std::optional<methods::SurfaceModel> model;
    std::optional<IlluminationRows> rows;
};
core::Result<void> prepare(ContinuousRun run, image::PlaneView<const std::uint8_t> protection,
                           Prepared& prepared) {
    const auto* const surface = std::get_if<methods::Surface>(&run.request.get().illumination());
    if (surface == nullptr) {
        return disabled_observations(run, protection);
    }
    auto model =
        methods::SurfaceModel::prepare({run.converter.get(), protection}, *surface,
                                       run.budget.get(), run.cancellation.get(), run.report.get());
    if (!model) {
        return std::unexpected(model.error());
    }
    if (!model->active()) {
        return {};
    }
    prepared.model = std::move(*model);
    auto block = image::Plane<double>::allocate(
        run.budget.get(), image::linear_block_pixels * image::rgb_channels, 1);
    if (!block) {
        return std::unexpected(block.error());
    }
    prepared.rows.emplace(run.converter.get(), std::move(*block),
                          IlluminationRun{
                              .model = *prepared.model,
                              .protection = protection,
                              .report = run.report.get(),
                              .cancellation = run.cancellation.get(),
                          });
    return {};
}
} // namespace
core::Result<app::PublishedImage> continuous(const app::ProcessRequest& request,
                                             image::Continuous operation,
                                             const core::Cancellation& cancellation,
                                             const bundle::RunContext& context,
                                             methods::IlluminationReport& illumination) {
    constexpr std::size_t continuous_budget_bytes = std::size_t{1024} * 1024 * 1024;
    core::Budget budget{continuous_budget_bytes};
    if (const auto* const method = std::get_if<methods::Surface>(&request.illumination())) {
        illumination.status = methods::SurfaceStatus::failed;
        illumination.requested = method->parameters();
    }
    auto decoded =
        io::load_source(request.input(), budget, operation.parameters().profile, cancellation);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    auto converter = color::Converter::create(decoded->raster, operation, budget, cancellation);
    if (!converter) {
        return std::unexpected(converter.error());
    }
    std::optional<MaskFacts> mask;
    image::Plane<std::uint8_t> protection;
    if (request.protection()) {
        auto loaded = io::load_protection_png(*request.protection(), (*converter)->extent(), budget,
                                              cancellation);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        protection = std::move(loaded->mask);
        mask = MaskFacts{
            .supplied = std::move(loaded->source),
            .canonical = protection.view().as_const(),
        };
    }
    Prepared prepared;
    auto ready = prepare(
        {
            .request = request,
            .converter = **converter,
            .budget = budget,
            .cancellation = cancellation,
            .report = illumination,
        },
        protection.view().as_const(), prepared);
    if (!ready) {
        return std::unexpected(ready.error());
    }
    image::RowSource& rows =
        prepared.rows ? static_cast<image::RowSource&>(*prepared.rows) : **converter;
    const Artwork artwork{std::ref(rows)};
    auto published = publish_run({
        .output_directory = request.output_directory(),
        .artwork = artwork,
        .budget = budget,
        .cancellation = cancellation,
        .context = context,
        .source = decoded->source,
        .source_name = io::file_name(request.input()),
        .source_decoding = decoded->description,
        .operation = operation,
        .mask = mask,
        .observe_conversion =
            [](void* state) { return static_cast<color::Converter*>(state)->report(); },
        .conversion_state = converter->get(),
        .illumination = illumination,
    });
    if (!published) {
        return std::unexpected(std::move(published.error()));
    }
    // The conversion the record states, so the response and the record cannot disagree.
    auto report = published->conversion.value_or((*converter)->report());
    // Derived from the comparison that ran, not asserted because publication returned.
    report.verified = published->verification == bundle::Verification::decoded_and_compared;
    return app::PublishedImage{
        .output = std::move(published->output),
        .conversion = report,
        .illumination = illumination,
        .run = std::move(published->run),
        .record = std::move(published->record),
        .source_decoding = decoded->description,
    };
}
} // namespace docenhance::host
