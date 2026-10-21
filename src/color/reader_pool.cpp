// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "context.hpp"
#include "conversion.hpp"
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <span>
#include <utility>

namespace docenhance::color {
InterpretationReader::InterpretationReader(InterpretationReaderState& state) noexcept
    : state_(&state) {}
InterpretationReader::InterpretationReader(InterpretationReader&& other) noexcept
    : state_(std::exchange(other.state_, nullptr)) {}
InterpretationReader& InterpretationReader::operator=(InterpretationReader&& other) noexcept {
    if (this != &other) {
        release();
        state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
}
InterpretationReader::~InterpretationReader() {
    release();
}
void InterpretationReader::release() noexcept {
    if (state_ != nullptr) {
        const MetadataLock lock{state_->run.get().reader_guard};
        state_->leased = false;
        state_ = nullptr;
    }
}
bool InterpretationReader::ready() const noexcept {
    return state_ != nullptr;
}
std::span<double> InterpretationReader::scratch() & noexcept {
    return state_ == nullptr ? std::span<double>{} : state_->linear.view().row(0);
}
core::Result<image::BlockObservation>
InterpretationReader::read_gray(std::uint32_t row, std::uint32_t first, std::span<double> output) {
    if (state_ == nullptr ||
        image::interpretation_category(state_->run.get().source.get().shape.model) !=
            image::InterpretationCategory::gray1) {
        return core::failure(core::ErrorCode::argument, "Source interpretation is not scalar gray");
    }
    return read_interpretation(*state_, row, first, output);
}
core::Result<image::BlockObservation>
InterpretationReader::read_rgb(std::uint32_t row, std::uint32_t first, std::span<double> output) {
    if (state_ == nullptr ||
        image::interpretation_category(state_->run.get().source.get().shape.model) !=
            image::InterpretationCategory::color3) {
        return core::failure(core::ErrorCode::argument, "Source interpretation is not RGB");
    }
    return read_interpretation(*state_, row, first, output);
}
} // namespace docenhance::color
