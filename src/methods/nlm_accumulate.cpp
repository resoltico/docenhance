// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "nlm_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::methods {
namespace {
core::Result<void> initialize_columns(const NlmContext& context, NlmBandStorage& storage) {
    const auto columns = storage.columns.view().row(0);
    const auto cancellation = context.working.scheduler.get().cancellation();
    for (std::uint32_t x = 0; x < columns.size(); ++x) {
        if (x % nlm_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        columns.subspan(x, 1).front() = 0;
    }
    for (std::uint32_t y = 0; y < context.method.get().parameters().patch; ++y) {
        const auto row = storage.horizontal.view().row(y);
        for (std::uint32_t x = 0; x < row.size(); ++x) {
            if (x % nlm_checkpoint_samples == 0 &&
                cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            columns.subspan(x, 1).front() += row.subspan(x, 1).front();
        }
    }
    return {};
}
core::Result<void> advance_columns(const NlmContext& context, NlmBandStorage& storage,
                                   std::uint32_t row) {
    const auto patch = context.method.get().parameters().patch;
    const auto entering = storage.horizontal.view().row(row + patch - 1);
    const auto leaving = storage.horizontal.view().row(row - 1);
    const auto columns = storage.columns.view().row(0);
    const auto cancellation = context.working.scheduler.get().cancellation();
    for (std::uint32_t x = 0; x < columns.size(); ++x) {
        if (x % nlm_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        columns.subspan(x, 1).front() +=
            entering.subspan(x, 1).front() - leaving.subspan(x, 1).front();
    }
    return {};
}
core::Result<void> accumulate_row(const NlmContext& context, NlmBand band, NlmOffset offset,
                                  NlmBandStorage& storage, std::uint32_t local) {
    const auto p = context.method.get().parameters();
    const auto radius = (p.patch + p.search - 2) / 2;
    const auto y = band.first + local;
    const auto xf = context.prepared.get().xfold.view().row(0);
    const auto yf = context.prepared.get().yfold.view().row(0);
    const auto neighbor_y =
        static_cast<std::size_t>(static_cast<std::int64_t>(y) + radius + offset.y);
    const auto neighbor =
        context.prepared.get().codes.view().row(yf.subspan(neighbor_y, 1).front());
    const auto weights = context.prepared.get().weights.view().row(0);
    const auto distances = storage.columns.view().row(0);
    const auto numerator = storage.accumulation.view().row(local);
    const auto denominator = storage.accumulation.view().row(local + band.rows);
    const auto mask = context.working.protection.empty() ? std::span<const std::uint8_t>{}
                                                         : context.working.protection.row(y);
    const auto cancellation = context.working.scheduler.get().cancellation();
    for (std::uint32_t x = 0; x < distances.size(); ++x) {
        if (x % nlm_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        const auto distance = distances.subspan(x, 1).front();
        if (distance >= weights.size()) {
            continue;
        }
        const auto weight = weights.subspan(distance, 1).front();
        const auto neighbor_x =
            static_cast<std::size_t>(static_cast<std::int64_t>(x) + radius + offset.x);
        numerator.subspan(x, 1).front() +=
            weight * neighbor.subspan(xf.subspan(neighbor_x, 1).front(), 1).front();
        denominator.subspan(x, 1).front() += weight;
    }
    return {};
}
} // namespace
core::Result<void> nlm_accumulate(const NlmContext& context, NlmBand band, NlmOffset offset,
                                  NlmBandStorage& storage) {
    if (auto initialized = initialize_columns(context, storage); !initialized) {
        return initialized;
    }
    for (std::uint32_t y = 0; y < band.rows; ++y) {
        if (y != 0) {
            if (auto advanced = advance_columns(context, storage, y); !advanced) {
                return advanced;
            }
        }
        if (auto accumulated = accumulate_row(context, band, offset, storage, y); !accumulated) {
            return accumulated;
        }
    }
    return {};
}
} // namespace docenhance::methods
