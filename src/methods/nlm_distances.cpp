// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "nlm_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
namespace docenhance::methods {
namespace {
core::Result<void> difference_row(const NlmContext& context, NlmOffset offset,
                                  std::uint32_t fold_row, std::span<std::uint32_t> differences) {
    const auto codes = context.prepared.get().codes.view();
    const auto xf = context.prepared.get().xfold.view().row(0);
    const auto yf = context.prepared.get().yfold.view().row(0);
    const auto rs = (context.method.get().parameters().search - 1) / 2;
    const auto first = codes.row(yf.subspan(fold_row, 1).front());
    const auto shifted = static_cast<std::size_t>(static_cast<std::int64_t>(fold_row) + offset.y);
    const auto second = codes.row(yf.subspan(shifted, 1).front());
    const auto cancellation = context.working.scheduler.get().cancellation();
    for (std::uint32_t c = 0; c < differences.size(); ++c) {
        if (c % nlm_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto a = first.subspan(xf.subspan(c + rs, 1).front(), 1).front();
        const auto shifted_x =
            static_cast<std::size_t>(static_cast<std::int64_t>(c) + rs + offset.x);
        const auto b = second.subspan(xf.subspan(shifted_x, 1).front(), 1).front();
        differences.subspan(c, 1).front() = static_cast<std::uint32_t>(
            std::abs(static_cast<std::int32_t>(a) - static_cast<std::int32_t>(b)));
    }
    return {};
}
core::Result<void> horizontal_row(const NlmContext& context,
                                  std::span<const std::uint32_t> differences,
                                  std::span<std::uint32_t> output) {
    const auto patch = context.method.get().parameters().patch;
    std::uint32_t sum = 0;
    for (std::uint32_t i = 0; i < patch; ++i) {
        sum += differences.subspan(i, 1).front();
    }
    const auto cancellation = context.working.scheduler.get().cancellation();
    for (std::uint32_t x = 0; x < output.size(); ++x) {
        if (x % nlm_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        if (x != 0) {
            sum += differences.subspan(x + patch - 1, 1).front() -
                   differences.subspan(x - 1, 1).front();
        }
        output.subspan(x, 1).front() = sum;
    }
    return {};
}
} // namespace
core::Result<void> nlm_distances(const NlmContext& context, NlmBand band, NlmOffset offset,
                                 NlmBandStorage& storage) {
    const auto rs = (context.method.get().parameters().search - 1) / 2;
    const auto differences = storage.differences.view().row(0);
    for (std::uint32_t y = 0; y < storage.horizontal.height(); ++y) {
        if (auto filled = difference_row(context, offset, band.first + y + rs, differences);
            !filled) {
            return filled;
        }
        if (auto summed = horizontal_row(context, differences, storage.horizontal.view().row(y));
            !summed) {
            return summed;
        }
    }
    return {};
}
} // namespace docenhance::methods
