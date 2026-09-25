// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace docenhance::color {
struct ConversionState;
// Opaque, context-local color engine. Source and budget must outlive this serialized row producer.
// There is no native type in the public API, process-global policy, or acceleration/thread plugin.
class Converter final : public image::RowSource, public image::LinearSource {
  public:
    [[nodiscard]] static core::Result<std::unique_ptr<Converter>>
    create(const image::Raster& source, image::Continuous operation, core::Budget& budget,
           const core::Cancellation& cancellation = {});
    Converter(const Converter&) = delete;
    Converter& operator=(const Converter&) = delete;
    Converter(Converter&&) = delete;
    Converter& operator=(Converter&&) = delete;
    ~Converter() override;
    [[nodiscard]] image::OutputDescriptor descriptor() const noexcept override;
    [[nodiscard]] core::Result<void> row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                         image::RowUse use) override;
    [[nodiscard]] image::ConversionReport report() const noexcept;
    [[nodiscard]] image::Extent extent() const noexcept override;
    [[nodiscard]] core::Result<void> read(image::RowRange range, std::span<double> rgb,
                                          image::RowUse use) override;

  private:
    explicit Converter(std::unique_ptr<ConversionState> state);
    std::unique_ptr<ConversionState> state_;
};
} // namespace docenhance::color
