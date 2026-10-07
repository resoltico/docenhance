// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <functional>
#include <optional>
#include <span>
namespace docenhance::host {
struct SharpenInput {
    std::reference_wrapper<image::LinearSource> source;
    image::PlaneView<const std::uint8_t> mask;
    bool prepared = false;
};
[[nodiscard]] core::Result<std::optional<methods::SharpenModel>>
prepare_sharpening(SharpenInput input, const methods::Sharpening& method, core::Budget& budget,
                   const core::Cancellation& cancellation, methods::SharpenReport& report);
class SharpenedSource final : public image::LinearSource {
  public:
    SharpenedSource(SharpenInput input, const methods::SharpenModel* model,
                    methods::SharpenReport& report, core::Cancellation cancellation)
        : input_(input), model_(model), report_(report), cancellation_(std::move(cancellation)) {}
    [[nodiscard]] image::Extent extent() const noexcept override {
        return input_.source.get().extent();
    }
    [[nodiscard]] bool active() const noexcept {
        return model_ != nullptr && model_->active();
    }
    [[nodiscard]] bool prepared() const noexcept {
        return input_.prepared || active();
    }
    [[nodiscard]] core::Result<void> read(image::RowRange range, std::span<double> rgb,
                                          image::RowUse use) override;

  private:
    SharpenInput input_;
    const methods::SharpenModel* model_;
    std::reference_wrapper<methods::SharpenReport> report_;
    core::Cancellation cancellation_;
};
[[nodiscard]] core::Result<void> assess_sharpening(SharpenedSource& source, core::Budget& budget,
                                                   const core::Cancellation& cancellation,
                                                   methods::SharpenReport& report);
} // namespace docenhance::host
