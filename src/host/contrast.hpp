// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
namespace docenhance::host {
struct ContrastInput {
    std::reference_wrapper<image::LinearSource> source;
    image::PlaneView<const std::uint8_t> mask;
    bool prepared = false;
};
[[nodiscard]] core::Result<std::optional<methods::ContrastModel>>
prepare_contrast(ContrastInput input, const methods::Contrast& method, core::Budget& budget,
                 const core::Cancellation& cancellation, methods::ContrastReport& report);
class ContrastedSource final : public image::LinearSource {
  public:
    ContrastedSource(ContrastInput input, const methods::ContrastModel* model,
                     methods::ContrastReport& report, core::Cancellation cancellation)
        : input_(input), model_(model), report_(report), cancellation_(std::move(cancellation)) {}
    [[nodiscard]] image::Extent extent() const noexcept override {
        return input_.source.get().extent();
    }
    [[nodiscard]] bool active() const noexcept {
        return model_ != nullptr && model_->active();
    }
    [[nodiscard]] bool prepared() const noexcept {
        return input_.prepared ||
               (model_ != nullptr && (model_->active() || model_->measured_source()));
    }
    [[nodiscard]] core::Result<void> read(image::RowRange range, std::span<double> rgb,
                                          image::RowUse use) override;

  private:
    ContrastInput input_;
    const methods::ContrastModel* model_;
    std::reference_wrapper<methods::ContrastReport> report_;
    core::Cancellation cancellation_;
};
[[nodiscard]] core::Result<void> assess_contrast(ContrastedSource& source, core::Budget& budget,
                                                 const core::Cancellation& cancellation,
                                                 methods::ContrastReport& report);
} // namespace docenhance::host
