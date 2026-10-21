// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "fft_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
namespace docenhance::fft {
namespace {
core::Result<void> check_output(const Transform& t, Axis axis, std::uint32_t first,
                                std::uint32_t count) {
    const auto cancel = t.scheduler.get().cancellation();
    const auto extent = t.spectrum.get().extent();
    const auto rows = axis == Axis::rows ? count : extent.height;
    const auto columns = axis == Axis::rows ? t.spectrum.get().shape().width : count;
    for (std::uint32_t y = 0; y < rows; ++y) {
        for (std::uint32_t x = 0; x < columns; ++x) {
            if (x % checkpoint_samples == 0 && cancel.requested(core::Checkpoint::processing)) {
                return failed(core::ErrorCode::cancelled);
            }
            const auto row = axis == Axis::rows ? first + y : y;
            const auto column = axis == Axis::columns ? first + x : x;
            const auto value = t.spectrum.get().row(row).subspan(column, 1).front();
            if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
                return failed(core::ErrorCode::numerical);
            }
        }
    }
    return {};
}
core::Result<void> check_real(const Transform& t, std::uint32_t first, std::uint32_t count) {
    const auto cancel = t.scheduler.get().cancellation();
    for (std::uint32_t y = first; y < first + count; ++y) {
        const auto row = t.output.row(y);
        for (std::size_t x = 0; x < row.size(); ++x) {
            if (x % checkpoint_samples == 0 && cancel.requested(core::Checkpoint::processing)) {
                return failed(core::ErrorCode::cancelled);
            }
            if (!std::isfinite(row.subspan(x, 1).front())) {
                return failed(core::ErrorCode::numerical);
            }
        }
    }
    return {};
}
core::Result<void> task(const Transform& t, Axis axis, std::size_t index) {
    const auto first = static_cast<std::uint32_t>(index * block_size);
    const auto total =
        axis == Axis::rows ? t.spectrum.get().extent().height : t.spectrum.get().shape().width;
    const auto count = std::min(block_size, total - first);
    const auto cancel = t.scheduler.get().cancellation();
    if (cancel.requested(core::Checkpoint::solving)) {
        return failed(core::ErrorCode::cancelled);
    }
    auto result =
        axis == Axis::rows ? native_rows(t, first, count) : native_columns(t, first, count);
    if (!result) {
        return result;
    }
    if (cancel.requested(core::Checkpoint::solving)) {
        return failed(core::ErrorCode::cancelled);
    }
    return t.inverse && axis == Axis::rows ? check_real(t, first, count)
                                           : check_output(t, axis, first, count);
}
} // namespace
core::Result<void> run_phase(const Transform& t, core::Budget& budget, Axis axis) {
    auto workers = t.scheduler.get().workers();
    auto charge = phase_bytes(t.spectrum.get().extent(), workers, axis);
    if (!charge) {
        return std::unexpected(std::move(charge.error()));
    }
    if (t.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return failed(core::ErrorCode::cancelled);
    }
    const auto available = budget.available();
    while (workers > exec::min_workers && *charge > available) {
        --workers;
        charge = phase_bytes(t.spectrum.get().extent(), workers, axis);
        if (!charge) {
            return std::unexpected(std::move(charge.error()));
        }
    }
    auto lease = budget.reserve(*charge);
    if (!lease) {
        return std::unexpected(std::move(lease.error()));
    }
    const auto concurrency = exec::Concurrency::resolve(workers, 1, 0, 0);
    if (!concurrency) {
        return std::unexpected(concurrency.error());
    }
    const exec::Scheduler scheduler{*concurrency, t.scheduler.get().cancellation()};
    const auto work = [&](std::size_t index) { return task(t, axis, index); };
    const auto n =
        axis == Axis::rows ? t.spectrum.get().extent().height : t.spectrum.get().shape().width;
    return scheduler.for_each(blocks(n), exec::WorkRef{work});
}
} // namespace docenhance::fft
