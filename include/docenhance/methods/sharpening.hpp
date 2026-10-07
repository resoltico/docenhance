// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/reviewed_methods.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
namespace docenhance::methods {
inline constexpr double unsharp_default_sigma = 0.8;
inline constexpr double unsharp_default_amount = 0.5;
struct UnsharpParameters {
    double sigma = unsharp_default_sigma;
    double amount = unsharp_default_amount;
    double threshold = 1;
    bool operator==(const UnsharpParameters&) const = default;
};
class Unsharp {
  public:
    static constexpr double minimum_sigma = 0.3;
    static constexpr double maximum_sigma = 3;
    static constexpr double maximum_amount = 2;
    static constexpr double maximum_threshold = 20;
    [[nodiscard]] static core::Result<Unsharp> create(UnsharpParameters p = {});
    [[nodiscard]] UnsharpParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return unsharp_descriptor;
    }

  private:
    explicit Unsharp(UnsharpParameters p) : parameters_(p) {}
    UnsharpParameters parameters_;
};
struct SharpenOff {};
using Sharpening = std::variant<SharpenOff, Unsharp>;
enum class SharpenStatus { disabled, no_change, applied, failed };
enum class SharpenReason { none, zero_amount, no_eligible_samples, no_effect, processing_failure };
struct ExcursionRange {
    double low = 0;
    double high = 0;
    bool operator==(const ExcursionRange&) const = default;
};
struct SharpenReport {
    SharpenStatus status = SharpenStatus::disabled;
    SharpenReason reason = SharpenReason::none;
    bool complete = false;
    std::optional<UnsharpParameters> requested = std::nullopt;
    std::optional<ExcursionRange> pre_clamp = std::nullopt;
    std::uint64_t eligible_samples = 0;
    std::uint64_t protected_samples = 0;
    std::uint64_t context_samples = 0;
    std::uint64_t evaluated_samples = 0;
    std::uint64_t corrected_samples = 0;
    std::uint64_t changed_samples = 0;
    std::uint64_t clipped_low_samples = 0;
    std::uint64_t clipped_high_samples = 0;
    std::uint64_t preparation_charge_peak = 0;
    bool operator==(const SharpenReport&) const = default;
};
[[nodiscard]] std::string_view status_name(SharpenStatus value) noexcept;
[[nodiscard]] std::string_view reason_name(SharpenReason value) noexcept;
[[nodiscard]] core::Result<double> unsharp_candidate(double f, double blurred,
                                                     const Unsharp& method);
[[nodiscard]] bool valid_sharpen_observations(const SharpenReport& report);
[[nodiscard]] bool valid_sharpen_extent(const SharpenReport& report, image::Extent extent);
[[nodiscard]] bool valid_sharpen(const SharpenReport& report, const Sharpening& method);
struct SharpenExecution {
    SharpenExecution(core::Budget& b, core::Cancellation c, SharpenReport& r, image::RowUse use)
        : budget(b), cancellation(std::move(c)), report(r), preparation_use(use) {}
    std::reference_wrapper<core::Budget> budget;
    core::Cancellation cancellation;
    std::reference_wrapper<SharpenReport> report;
    image::RowUse preparation_use;
};
class SharpenModel {
  public:
    [[nodiscard]] static core::Result<SharpenModel>
    prepare(image::LinearSource& source, image::PlaneView<const std::uint8_t> mask,
            const Unsharp& method, const SharpenExecution& execution);
    [[nodiscard]] bool active() const noexcept {
        return !blurred_.view().empty();
    }
    [[nodiscard]] core::Result<void> apply(image::RowRange range, std::span<double> rgb,
                                           image::PlaneView<const std::uint8_t> mask,
                                           SharpenReport& report,
                                           const core::Cancellation& cancellation) const;

  private:
    [[nodiscard]] core::Result<image::Rgb>
    map_pixel(const image::Rgb& before, image::RowRange position, SharpenReport& report) const;
    SharpenModel(image::Extent extent, Unsharp method, image::Plane<double> blurred)
        : extent_(extent), method_(method), blurred_(std::move(blurred)) {}
    image::Extent extent_;
    Unsharp method_;
    image::Plane<double> blurred_;
};
} // namespace docenhance::methods
