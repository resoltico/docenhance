// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "tvl1_fixture.hpp"
#include "tvl1_reference.hpp"
#include "tvl1_state_bounds.hpp"
#include "tvl1_working_reference.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stop_token>
#include <vector>

namespace docenhance::tests {
inline std::vector<std::uint64_t> tvl1_state_bits(TvPlanes& planes) {
    const auto field = planes.field();
    const std::array views{
        field.input,
        field.primal.as_const(),
        field.extrapolated.as_const(),
        field.dual_x.as_const(),
        field.dual_y.as_const(),
    };
    std::vector<std::uint64_t> result;
    result.reserve(std::size_t{field.input.width()} * field.input.height() * views.size());
    for (const auto view : views) {
        for (std::uint32_t y = 0; y < view.height(); ++y) {
            const auto row = gamma_bits(view.row(y));
            result.insert(result.end(), row.begin(), row.end());
        }
    }
    return result;
}
inline void tvl1_parallel_state_case() {
    core::Budget budget{std::size_t{2} * 1024 * 1024};
    constexpr image::Extent extent{.width = 33, .height = 137};
    std::array<TvPlanes, 3> planes{
        TvPlanes{budget, extent},
        TvPlanes{budget, extent},
        TvPlanes{budget, extent},
    };
    std::vector<double> input;
    input.reserve(std::size_t{extent.width} * extent.height);
    for (std::size_t i = 0; i < std::size_t{extent.width} * extent.height; ++i) {
        input.push_back(static_cast<double>((i * 17) % 997) / 1000);
    }
    for (auto& state : planes) {
        state.fill(input);
    }
    const std::array schedulers{
        exec::Scheduler{exec::Concurrency::resolve(1, 7, 0, 0).value()},
        exec::Scheduler{exec::Concurrency::resolve(2, 7, 0, 0).value()},
        exec::Scheduler{exec::Concurrency::resolve(7, 7, 0, 0).value()},
    };
    TvReference reference{extent.width, extent.height, input};
    for (std::uint32_t i = 0; i < 20; ++i) {
        const auto expected = tvl1_reference_step(reference, 0.05);
        std::array<methods::Tvl1Updates, 3> updates{};
        for (std::size_t worker = 0; worker < planes.size(); ++worker) {
            auto& state = planes.at(worker);
            require(tvl1_state_bounded(state.field(), input),
                    "actual parallel step begins in supported state");
            const auto result = methods::tvl1_iteration(state.field(), 0.05, schedulers.at(worker));
            require(result.has_value() && tvl1_state_bounded(state.field(), input),
                    "every actual dual/primal joined step preserves exact8eps/Unit/bar/terminal "
                    "properties");
            updates.at(worker) = *result;
        }
        const auto first = tvl1_state_bits(planes.front());
        for (std::size_t worker = 1; worker < planes.size(); ++worker) {
            require(first == tvl1_state_bits(planes.at(worker)) &&
                        updates.at(worker).primal == updates.front().primal &&
                        updates.at(worker).dual == updates.front().dual,
                    "all five logical planes and both maximum metrics match bitwise after "
                    "each1/2/7 step");
        }
        require(std::abs(updates.front().primal - expected.primal) < 1e-12 &&
                    std::abs(updates.front().dual - expected.dual) < 1e-12,
                "actual maximum updates agree with simultaneous double-buffer reference");
        const auto view = planes.front().u.view();
        for (std::uint32_t y = 0; y < view.height(); ++y) {
            for (std::uint32_t x = 0; x < view.width(); ++x) {
                require(std::abs(view.row(y).subspan(x, 1).front() -
                                 reference.primal.at((std::size_t{y} * view.width()) + x)) < 1e-12,
                        "actual seam-crossing primal agrees with independent scattered adjoint");
            }
        }
    }
}
inline void tvl1_objective_admission_case() {
    core::Budget budget{std::size_t{2} * 1024 * 1024};
    TvPlanes planes{budget, {.width = 3, .height = 2}};
    const std::vector<double> input{0.1, 0.2, 0.3, 0.4, 0.5, 0.6};
    planes.fill(input);
    const exec::Scheduler healthy{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto energy = methods::tvl1_objective(planes.f.view().as_const(),
                                                planes.u.view().as_const(), 1.5, healthy);
    require(energy && std::isfinite(*energy) && *energy > 0,
            "healthy scheduled ordered objective actually computes positive finite energy");
    std::stop_source stop;
    request_stop(stop);
    const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                  core::Cancellation{stop.get_token()}};
    const auto cancelled = methods::tvl1_objective(planes.f.view().as_const(),
                                                   planes.u.view().as_const(), 1.5, stopped);
    require(!cancelled && cancelled.error().code == core::ErrorCode::cancelled,
            "valid objective metadata observes real scheduler cancellation");
    const auto wrong_shape =
        image::PlaneView<const double>::create(
            planes.u.view().row(0), {.width = 3, .height = 1, .stride = 3 * sizeof(double)})
            .value();
    const auto mismatch =
        methods::tvl1_objective(planes.f.view().as_const(), wrong_shape, 1.5, stopped);
    require(!mismatch && mismatch.error().code == core::ErrorCode::argument,
            "objective shape mismatch precedes scheduling despite a pending stop");
    for (const auto lambda : {0.01, 20.01, std::numeric_limits<double>::quiet_NaN()}) {
        const auto invalid = methods::tvl1_objective(planes.f.view().as_const(),
                                                     planes.u.view().as_const(), lambda, stopped);
        require(!invalid && invalid.error().code == core::ErrorCode::argument,
                "objective invalid fidelity precedes scheduling with otherwise valid Unit fields");
    }
}
} // namespace docenhance::tests
