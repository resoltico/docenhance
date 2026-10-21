// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/fft/fft.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "fft_detail.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <new>
#include <span>
#include <utility>
namespace docenhance::fft {
core::Result<void> copy_spectrum(const Spectrum& from, Spectrum& to,
                                 const core::Cancellation& cancellation) {
    for (std::uint32_t y = 0; y < from.shape().height; ++y) {
        const auto source = from.row(y);
        const auto target = to.row(y);
        for (std::size_t x = 0; x < source.size(); ++x) {
            if (x % checkpoint_samples == 0 &&
                cancellation.requested(core::Checkpoint::processing)) {
                return failed(core::ErrorCode::cancelled);
            }
            const auto value = source.subspan(x, 1).front();
            if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
                return failed(core::ErrorCode::numerical);
            }
            target.subspan(x, 1).front() = value;
        }
    }
    return {};
}
core::Result<Spectrum> forward(image::PlaneView<const double> input, core::Budget& budget,
                               const exec::Scheduler& scheduler) {
    try {
        if (!valid_real(input)) {
            return failed(core::ErrorCode::argument);
        }
        const image::Extent extent{.width = input.width(), .height = input.height()};
        auto result = Spectrum::allocate(budget, extent, scheduler.cancellation());
        if (!result) {
            return result;
        }
        const Transform t{input, {}, *result, scheduler, false};
        if (auto phase = run_phase(t, budget, Axis::rows); !phase) {
            return std::unexpected(std::move(phase.error()));
        }
        if (auto phase = run_phase(t, budget, Axis::columns); !phase) {
            return std::unexpected(std::move(phase.error()));
        }
        return result;
    } catch (const std::bad_alloc&) {
        return failed(core::ErrorCode::resource);
    } catch (...) {
        return failed(core::ErrorCode::invariant);
    }
}
core::Result<void> inverse(const Spectrum& input, image::PlaneView<double> output,
                           core::Budget& budget, const exec::Scheduler& scheduler) {
    try {
        if (input.empty() || !valid_real(output.as_const()) ||
            input.extent() != image::Extent{.width = output.width(), .height = output.height()} ||
            overlaps(std::as_bytes(input.storage()), std::as_bytes(output.storage()))) {
            return failed(core::ErrorCode::argument);
        }
        auto work = Spectrum::allocate(budget, input.extent(), scheduler.cancellation());
        if (!work) {
            return std::unexpected(std::move(work.error()));
        }
        if (auto copy = copy_spectrum(input, *work, scheduler.cancellation()); !copy) {
            return copy;
        }
        const Transform t{{}, output, *work, scheduler, true};
        if (auto phase = run_phase(t, budget, Axis::columns); !phase) {
            return phase;
        }
        return run_phase(t, budget, Axis::rows);
    } catch (const std::bad_alloc&) {
        return failed(core::ErrorCode::resource);
    } catch (...) {
        return failed(core::ErrorCode::invariant);
    }
}
} // namespace docenhance::fft
