// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/color/converter.hpp"

#include "context.hpp"
#include "conversion.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <utility>

namespace docenhance::color {
Converter::Converter(std::unique_ptr<ConversionState> state) : state_(std::move(state)) {}
Converter::~Converter() = default;
core::Result<std::unique_ptr<Converter>> Converter::create(const image::Raster& source,
                                                           image::Continuous operation,
                                                           core::Budget& budget,
                                                           const core::Cancellation& cancellation) {
    auto bytes = image::raster_row_bytes(source.shape);
    constexpr unsigned last_orientation = 8;
    if (!bytes || source.pixels.empty() || source.pixels.width() != *bytes ||
        source.pixels.height() != source.shape.height || source.metadata.orientation == 0 ||
        source.metadata.orientation > last_orientation) {
        return core::failure(core::ErrorCode::argument,
                             "Color conversion requires a valid decoded raster");
    }
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto state = std::make_unique<ConversionState>(source, operation, budget, cancellation);
    if (!state->context.good()) {
        return std::unexpected(state->context.error("Cannot create bounded color context"));
    }
    auto prepared = prepare_conversion(*state, budget);
    if (!prepared) {
        return std::unexpected(prepared.error());
    }
    return std::unique_ptr<Converter>{new Converter{std::move(state)}};
}
image::OutputDescriptor Converter::descriptor() const noexcept {
    // uint8_t observes serialized bytes without changing their representation.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(state_->profile.bytes().data());
    return {
        .shape = state_->report.output,
        .profile = {bytes, state_->profile.size()},
        .resolution = state_->report.resolution,
    };
}
core::Result<void> Converter::row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                  image::RowUse use) {
    return convert_row(*state_, index, bytes, use);
}
image::ConversionReport Converter::report() const noexcept {
    return state_->report;
}
image::Extent Converter::extent() const noexcept {
    return {.width = state_->report.output.width, .height = state_->report.output.height};
}
core::Result<void> Converter::read(image::RowRange range, std::span<double> rgb,
                                   image::RowUse use) {
    return read_linear(*state_, range, rgb, use);
}
} // namespace docenhance::color
