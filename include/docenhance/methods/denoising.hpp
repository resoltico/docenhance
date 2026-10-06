// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/reviewed_methods.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
namespace docenhance::methods {
inline constexpr double nlm_default_h = 3;
inline constexpr std::uint32_t nlm_default_patch = 7;
inline constexpr std::uint32_t nlm_default_search = 21;
inline constexpr double denoising_default_blend = 0.5;
inline constexpr double nlm_min_h = 0.1;
inline constexpr double nlm_max_h = 25;
inline constexpr std::uint32_t nlm_min_patch = 3;
inline constexpr std::uint32_t nlm_max_patch = 15;
inline constexpr std::uint32_t nlm_min_search = 7;
inline constexpr std::uint32_t nlm_max_search = 41;
struct NlmParameters {
    double h = nlm_default_h;
    std::uint32_t patch = nlm_default_patch;
    std::uint32_t search = nlm_default_search;
    double blend = denoising_default_blend;
    bool operator==(const NlmParameters&) const = default;
};
class Nlm {
  public:
    [[nodiscard]] static core::Result<Nlm> create(NlmParameters parameters = {});
    [[nodiscard]] NlmParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return nlm_descriptor;
    }

  private:
    explicit Nlm(NlmParameters parameters) : parameters_(parameters) {}
    NlmParameters parameters_;
};
struct DenoisingOff {};
inline constexpr double tvl1_default_lambda = 1.5;
inline constexpr std::uint32_t tvl1_default_iterations = 150;
inline constexpr double tvl1_default_tolerance = 1e-5;
struct Tvl1Parameters {
    double lambda = tvl1_default_lambda;
    std::uint32_t iterations = tvl1_default_iterations;
    double tolerance = tvl1_default_tolerance;
    double blend = denoising_default_blend;
    bool operator==(const Tvl1Parameters&) const = default;
};
class Tvl1 {
  public:
    static constexpr double minimum_lambda = 0.05;
    static constexpr double maximum_lambda = 20;
    static constexpr std::uint32_t minimum_iterations = 10;
    static constexpr std::uint32_t maximum_iterations = 1000;
    static constexpr double minimum_tolerance = 1e-8;
    static constexpr double maximum_tolerance = 1e-3;
    [[nodiscard]] static core::Result<Tvl1> create(Tvl1Parameters parameters = {});
    [[nodiscard]] Tvl1Parameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return tvl1_descriptor;
    }

  private:
    explicit Tvl1(Tvl1Parameters parameters) : parameters_(parameters) {}
    Tvl1Parameters parameters_;
};
using Denoising = std::variant<DenoisingOff, Nlm, Tvl1>;
using DenoisingParameters = std::variant<NlmParameters, Tvl1Parameters>;
enum class Tvl1Stop { tolerance_met, iteration_limit };
struct Tvl1Report {
    std::uint32_t iterations = 0;
    std::optional<Tvl1Stop> stop = std::nullopt;
    std::uint32_t passing_checkpoints = 0;
    double primal_update = 0;
    double dual_update = 0;
    std::optional<double> objective_start = std::nullopt;
    std::optional<double> objective_end = std::nullopt;
    std::uint64_t field_bytes = 0;
    bool operator==(const Tvl1Report&) const = default;
};
enum class DenoiseStatus { disabled, no_change, applied, failed };
enum class DenoiseReason { none, zero_blend, no_eligible_samples, no_effect, processing_failure };
inline constexpr std::uint32_t nlm_tile_width = 256;
inline constexpr std::uint32_t nlm_native_extent =
    nlm_tile_width + nlm_max_patch + nlm_max_search - 2;
struct DenoisingReport {
    DenoiseStatus status = DenoiseStatus::disabled;
    DenoiseReason reason = DenoiseReason::none;
    bool complete = false;
    std::optional<DenoisingParameters> requested = std::nullopt;
    std::optional<Tvl1Report> tvl1 = std::nullopt;
    double native_h = 0;
    std::uint64_t eligible_samples = 0;
    std::uint64_t protected_samples = 0;
    std::uint64_t evaluated_samples = 0;
    std::uint64_t corrected_samples = 0;
    std::uint64_t changed_samples = 0;
    std::uint64_t native_calls = 0;
    std::uint64_t native_reserved_peak = 0;
    std::uint64_t preparation_charge_peak = 0;
    bool operator==(const DenoisingReport&) const = default;
};
[[nodiscard]] std::string_view status_name(DenoiseStatus status) noexcept;
[[nodiscard]] std::string_view reason_name(DenoiseReason reason) noexcept;
[[nodiscard]] double nlm_native_strength(const NlmParameters& parameters) noexcept;
[[nodiscard]] core::Result<std::uint16_t> nlm_quantize(const image::Rgb& rgb);
[[nodiscard]] core::Result<image::Rgb> nlm_correct(const image::Rgb& rgb, std::uint16_t input,
                                                   std::uint16_t output, double blend);
[[nodiscard]] core::Result<std::size_t> nlm_native_scratch(image::Extent e, const Nlm& method);
[[nodiscard]] bool valid_denoising(const DenoisingReport& report, const Denoising& requested);
[[nodiscard]] bool valid_denoising_extent(const DenoisingReport& report, image::Extent extent);
} // namespace docenhance::methods
