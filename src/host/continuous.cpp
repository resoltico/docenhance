// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "continuous.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/protection_png.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "illumination_rows.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
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
core::Result<std::string> publish(ContinuousRun run,
                                  image::PlaneView<const std::uint8_t> protection) {
    const auto* const surface = std::get_if<methods::Surface>(&run.request.get().illumination());
    if (surface == nullptr) {
        auto observed = disabled_observations(run, protection);
        if (!observed) {
            return std::unexpected(observed.error());
        }
        return io::publish_png_rows(run.request.get().output_directory(), run.converter.get(),
                                    run.budget.get(), run.cancellation.get());
    }
    auto model = methods::SurfaceModel::prepare(
        {.source = run.converter.get(), .protection = protection}, *surface, run.budget.get(),
        run.cancellation.get(), run.report.get());
    if (!model) {
        return std::unexpected(model.error());
    }
    if (!model->active()) {
        return io::publish_png_rows(run.request.get().output_directory(), run.converter.get(),
                                    run.budget.get(), run.cancellation.get());
    }
    auto block = image::Plane<double>::allocate(
        run.budget.get(), image::linear_block_pixels * image::rgb_channels, 1);
    if (!block) {
        return std::unexpected(block.error());
    }
    IlluminationRows rows{run.converter.get(),
                          std::move(*block),
                          {
                              .model = *model,
                              .protection = protection,
                              .report = run.report.get(),
                              .cancellation = run.cancellation.get(),
                          }};
    return io::publish_png_rows(run.request.get().output_directory(), rows, run.budget.get(),
                                run.cancellation.get());
}
} // namespace
core::Result<app::PublishedImage> continuous(const app::ProcessRequest& request,
                                             image::Continuous operation,
                                             const core::Cancellation& cancellation,
                                             methods::IlluminationReport& illumination) {
    constexpr std::size_t continuous_budget_bytes = std::size_t{1024} * 1024 * 1024;
    core::Budget budget{continuous_budget_bytes};
    if (const auto* const method = std::get_if<methods::Surface>(&request.illumination())) {
        illumination.status = methods::SurfaceStatus::failed;
        illumination.requested = method->parameters();
    }
    auto source =
        io::load_png_raster(request.input(), budget, operation.parameters().profile, cancellation);
    if (!source) {
        return std::unexpected(source.error());
    }
    auto converter = color::Converter::create(*source, operation, budget, cancellation);
    if (!converter) {
        return std::unexpected(converter.error());
    }
    image::Plane<std::uint8_t> protection;
    if (request.protection()) {
        auto loaded = io::load_protection_png(*request.protection(), (*converter)->extent(), budget,
                                              cancellation);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        protection = std::move(*loaded);
    }
    auto published = publish(
        {
            .request = request,
            .converter = **converter,
            .budget = budget,
            .cancellation = cancellation,
            .report = illumination,
        },
        protection.view().as_const());
    if (!published) {
        return std::unexpected(published.error());
    }
    auto report = (*converter)->report();
    report.verified = true;
    return app::PublishedImage{
        .output = std::move(*published),
        .conversion = report,
        .illumination = illumination,
    };
}
} // namespace docenhance::host
