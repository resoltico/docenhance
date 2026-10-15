// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/progress.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
namespace docenhance::methods {
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
class Tvl1 {
  public:
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
enum class DenoiseStatus { unresolved, disabled, no_change, applied, failed };
enum class DenoiseReason { none, zero_blend, no_eligible_samples, no_effect, processing_failure };
inline constexpr std::uint32_t nlm_tile_width = 256;
inline constexpr std::uint32_t nlm_native_extent =
    nlm_tile_width + nlm_patch_domain.maximum + nlm_search_domain.maximum - 2;
struct DenoisingReport {
    DenoiseStatus status = DenoiseStatus::disabled;
    DenoiseReason reason = DenoiseReason::none;
    StageProgress progress = StageProgress::not_started;
    [[nodiscard]] bool complete() const noexcept {
        return progress == StageProgress::completed;
    }
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
