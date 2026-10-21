// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/working.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace docenhance::methods {
inline constexpr std::size_t tvl1_row_bands_max = 64;
[[nodiscard]] inline std::size_t tvl1_band_count(std::uint32_t height) noexcept {
    return std::min(tvl1_row_bands_max, static_cast<std::size_t>(height));
}
// Positive height and an admitted band index; partition depends only on extent.
[[nodiscard]] inline std::pair<std::uint32_t, std::uint32_t> tvl1_rows(std::uint32_t height,
                                                                       std::size_t band) noexcept {
    const auto bands = tvl1_band_count(height);
    return {
        static_cast<std::uint32_t>(std::uint64_t{band} * height / bands),
        static_cast<std::uint32_t>(std::uint64_t{band + 1} * height / bands),
    };
}
// Private numerical solve state. This is not a report, requested stage or wire object.
struct Tvl1SolveState {
    std::uint32_t iterations{};
    std::optional<Tvl1Stop> stop;
    std::uint32_t passing_checkpoints{};
    double primal_update{};
    double dual_update{};
    std::optional<double> objective_start;
    std::optional<double> objective_end;
};
[[nodiscard]] bool tvl1_checkpoint(Tvl1SolveState& report, const Tvl1& method) noexcept;
[[nodiscard]] core::Result<void> solve_tvl1(Tvl1Field field, const Tvl1& method,
                                            const exec::Scheduler& scheduler,
                                            Tvl1SolveState& report);
struct Tvl1Fit {
    image::Plane<double> input;
    image::Plane<double> primal;
    Tvl1Solution solution;
};
// Declared after progress, so ordinary return and unwind capture before progress dies.
class Tvl1StopCapture {
  public:
    Tvl1StopCapture(const Tvl1SolveState& progress, std::optional<Tvl1Stop>& observed_stop) noexcept
        : progress_(progress), observed_stop_(observed_stop) {}
    Tvl1StopCapture(const Tvl1StopCapture&) = delete;
    Tvl1StopCapture& operator=(const Tvl1StopCapture&) = delete;
    Tvl1StopCapture(Tvl1StopCapture&&) = delete;
    Tvl1StopCapture& operator=(Tvl1StopCapture&&) = delete;
    ~Tvl1StopCapture() noexcept {
        observed_stop_.get() = progress_.get().stop;
    }

  private:
    std::reference_wrapper<const Tvl1SolveState> progress_;
    std::reference_wrapper<std::optional<Tvl1Stop>> observed_stop_;
};
[[nodiscard]] core::Result<Tvl1Fit> fit_tvl1(const Tvl1& method, Working working,
                                             std::optional<Tvl1Stop>& observed_stop);
} // namespace docenhance::methods
