// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/stages.hpp"
#include "docenhance/methods/surface.hpp"

#include <functional>
#include <span>
#include <utility>
#include <variant>
namespace docenhance::host {
// A failed upstream application owns a delegated read error. The consumer entered
// preparation, but did not produce a usable model or independently fail its method.
template <class Report>
void delegated_preparation_failure(Report& consumer, const methods::StageObservations& stages) {
    const auto read_failed = [](const auto& report) {
        return report.progress == methods::StageProgress::evaluating &&
               (report.status == decltype(report.status)::failed ||
                report.status == decltype(report.status)::unresolved);
    };
    if (consumer.progress == methods::StageProgress::preparing &&
        (read_failed(stages.illumination) || read_failed(stages.denoising) ||
         read_failed(stages.restoration) || read_failed(stages.contrast))) {
        consumer.status = decltype(consumer.status)::unresolved;
        consumer.reason = decltype(consumer.reason)::none;
    }
}
// Traversal owns only the charged block and bounded processing checkpoints.
[[nodiscard]] core::Result<void> observe_rows(image::LinearSource& source, core::Budget& budget,
                                              const core::Cancellation& cancellation,
                                              bool allocation_checkpoint, std::uint64_t* live_peak);
using IlluminationModel = std::variant<methods::SurfaceModel, methods::MorphologyModel>;
class IlluminatedSource final : public image::LinearSource {
  public:
    IlluminatedSource(color::Converter& source, const IlluminationModel* model,
                      image::PlaneView<const std::uint8_t> protection,
                      methods::IlluminationReport& report, core::Cancellation cancellation)
        : source_(source), model_(model), protection_(protection), report_(report),
          cancellation_(std::move(cancellation)) {}
    [[nodiscard]] image::Extent extent() const noexcept override {
        return source_.get().extent();
    }
    [[nodiscard]] core::Result<void> read(image::RowRange range, std::span<double> rgb,
                                          image::RowUse use) override;

  private:
    void advance_output(std::uint32_t count) noexcept;
    std::reference_wrapper<color::Converter> source_;
    const IlluminationModel* model_;
    image::PlaneView<const std::uint8_t> protection_;
    std::reference_wrapper<methods::IlluminationReport> report_;
    core::Cancellation cancellation_;
    image::RowRange next_output_{};
};
class ContinuousRows final : public image::RowSource {
  public:
    ContinuousRows(image::LinearSource& source, image::OutputDescriptor descriptor,
                   image::Plane<double> block, bool prefix_observed)
        : source_(source), descriptor_(descriptor), block_(std::move(block)),
          prefix_observed_(prefix_observed) {}
    [[nodiscard]] image::OutputDescriptor descriptor() const noexcept override {
        return descriptor_;
    }
    [[nodiscard]] core::Result<void> row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                         image::RowUse use) override;

  private:
    std::reference_wrapper<image::LinearSource> source_;
    image::OutputDescriptor descriptor_;
    image::Plane<double> block_;
    bool prefix_observed_;
};
} // namespace docenhance::host
