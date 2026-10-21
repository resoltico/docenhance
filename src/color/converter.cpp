// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "context.hpp"
#include "conversion.hpp"
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <expected>
#include <memory>
#include <utility>

namespace docenhance::color {
namespace {
core::Result<std::unique_ptr<InterpretationState>>
fit_readers(std::unique_ptr<InterpretationState> state, core::Budget& budget, unsigned workers) {
    const auto source = state->source;
    const auto policy = state->parameters;
    const auto cancellation = state->cancellation;
    const auto output = state->report.output;
    for (;;) {
        auto prepared = prepare_readers(*state, budget, workers);
        if (prepared) {
            return state;
        }
        if (prepared.error().code != core::ErrorCode::resource || !state->native ||
            state->readers.empty() || workers == 1) {
            return std::unexpected(std::move(prepared.error()));
        }
        workers = std::min(workers / 2, static_cast<unsigned>(state->readers.size()));
        // Native exhaustion poisons the whole context. Refund every owner before rebuilding it.
        state.reset();
        if (cancellation.requested(core::Checkpoint::allocation)) {
            return core::cancelled();
        }
        state = std::make_unique<InterpretationState>(source.get(), policy, budget, cancellation);
        state->report.output = output;
        if (!state->context.good()) {
            return std::unexpected(state->context.error("Cannot create bounded color context"));
        }
        if (auto rebuilt = prepare_interpretation(*state); !rebuilt) {
            return std::unexpected(std::move(rebuilt.error()));
        }
    }
}
} // namespace
Interpretation::Interpretation(std::unique_ptr<InterpretationState> state)
    : state_(std::move(state)) {}
Interpretation::~Interpretation() = default;
core::Result<std::unique_ptr<Interpretation>>
Interpretation::create(const image::Raster& source, image::InterpretationPolicy policy,
                       image::PngShape output, core::Budget& budget, const ReadControl& control) {
    const auto& cancellation = control.cancellation;
    if (control.workers < core::min_execution_threads ||
        control.workers > core::max_execution_threads) {
        return core::failure(core::ErrorCode::argument, "Invalid interpretation worker count");
    }
    const auto bytes = image::raster_row_bytes(source.shape);
    const auto oriented = image::rotated_shape(
        image::oriented_shape(source.shape, source.metadata.orientation), policy.rotation);
    if (!bytes || source.pixels.empty() || source.pixels.width() != *bytes ||
        source.pixels.height() != source.shape.height ||
        !image::valid_interpretation_policy(policy) || !image::validate(output) ||
        output.width != oriented.width || output.height != oriented.height ||
        (!image::is_color(source.shape.model) && output.model != image::SampleModel::gray)) {
        return core::failure(core::ErrorCode::argument,
                             "Interpretation requires valid matching raster and output shapes");
    }
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto state = std::make_unique<InterpretationState>(source, policy, budget, cancellation);
    state->report.output = output;
    if (!state->context.good()) {
        return std::unexpected(state->context.error("Cannot create bounded color context"));
    }
    if (policy.profile != image::ProfilePolicy::srgb && source.metadata.png() != nullptr) {
        if (auto valid = validate_declarations(source.metadata); !valid) {
            return std::unexpected(std::move(valid.error()));
        }
    }
    if (auto prepared = prepare_interpretation(*state); !prepared) {
        return std::unexpected(std::move(prepared.error()));
    }
    auto fitted = fit_readers(std::move(state), budget, control.workers);
    if (!fitted) {
        return std::unexpected(std::move(fitted.error()));
    }
    return std::unique_ptr<Interpretation>{new Interpretation{std::move(*fitted)}};
}
const image::InterpretationReport& Interpretation::descriptor() const& noexcept {
    return state_->report;
}
image::InterpretationCategory Interpretation::category() const noexcept {
    return image::interpretation_category(state_->source.get().shape.model);
}
unsigned Interpretation::workers() const noexcept {
    return static_cast<unsigned>(state_->readers.size());
}
core::Result<InterpretationReader> Interpretation::reader() & {
    const MetadataLock lock{state_->reader_guard};
    for (const auto& reader : state_->readers) {
        if (!reader->leased) {
            reader->leased = true;
            return InterpretationReader{*reader};
        }
    }
    return core::failure(core::ErrorCode::invariant,
                         "Every interpretation reader is already leased");
}
} // namespace docenhance::color
