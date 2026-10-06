// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <utility>
namespace docenhance::methods {
inline constexpr double tvl1_step = 0.25;
inline constexpr std::uint32_t tvl1_first_checkpoint = 20;
inline constexpr std::uint32_t tvl1_checkpoint_period = 10;
inline constexpr std::uint32_t tvl1_required_passes = 2;
struct Tvl1Field {
    image::PlaneView<const double> input;
    image::PlaneView<double> primal;
    image::PlaneView<double> extrapolated;
    image::PlaneView<double> dual_x;
    image::PlaneView<double> dual_y;
};
struct Tvl1Updates {
    double primal{};
    double dual{};
};
[[nodiscard]] core::Result<Tvl1Updates> tvl1_iteration(Tvl1Field field, double lambda,
                                                       const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<double> tvl1_objective(image::PlaneView<const double> input,
                                                  image::PlaneView<const double> primal,
                                                  double lambda,
                                                  const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<image::Rgb> tvl1_correct(const image::Rgb& rgb, double input,
                                                    double output, double blend);
[[nodiscard]] bool valid_tvl1_observations(const DenoisingReport& report);
[[nodiscard]] bool valid_tvl1_report(const DenoisingReport& report, const Tvl1& method);
struct Tvl1Execution {
    Tvl1Execution(core::Budget& budget_value, core::Cancellation cancellation_value,
                  DenoisingReport& report_value)
        : budget(budget_value), cancellation(std::move(cancellation_value)), report(report_value) {}
    std::reference_wrapper<core::Budget> budget;
    core::Cancellation cancellation;
    std::reference_wrapper<DenoisingReport> report;
};
class Tvl1Model {
  public:
    [[nodiscard]] static core::Result<Tvl1Model>
    prepare(image::LinearSource& source, image::PlaneView<const std::uint8_t> protection,
            const Tvl1& method, const Tvl1Execution& execution);
    ~Tvl1Model() = default;
    Tvl1Model(const Tvl1Model&) = delete;
    Tvl1Model& operator=(const Tvl1Model&) = delete;
    Tvl1Model(Tvl1Model&&) noexcept = default;
    Tvl1Model& operator=(Tvl1Model&&) noexcept = default;
    [[nodiscard]] bool active() const noexcept {
        return !input_.empty();
    }
    [[nodiscard]] image::PlaneView<const double> input() const noexcept {
        return input_.view();
    }
    [[nodiscard]] image::PlaneView<const double> result() const noexcept {
        return result_.view();
    }
    [[nodiscard]] core::Result<void> apply(image::RowRange range, std::span<double> rgb,
                                           image::PlaneView<const std::uint8_t> protection,
                                           DenoisingReport& report,
                                           const core::Cancellation& cancellation) const;

  private:
    Tvl1Model(image::Plane<double> input, image::Plane<double> result, double blend)
        : input_(std::move(input)), result_(std::move(result)), blend_(blend) {}
    image::Plane<double> input_;
    image::Plane<double> result_;
    double blend_{};
};
} // namespace docenhance::methods
