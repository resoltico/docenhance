// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"
#include "docenhance/methods/parameters.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <variant>
#include <vector>
namespace docenhance::methods {
inline constexpr std::uint32_t gaussian_psf_radius_sigmas = 3;
inline constexpr std::uint32_t motion_psf_minimum_samples = 64;
inline constexpr std::uint32_t motion_psf_samples_per_pixel = 32;
inline constexpr double psf_off_center_limit = 0.25;
inline constexpr std::uint32_t psf_minimum_side = 3;
inline constexpr std::uint32_t psf_maximum_side = core::psf_dimension_max;
inline constexpr std::uint32_t fft_max_guard = 256;
inline constexpr auto restoration_fft_maximum =
    static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 2);
inline constexpr double psf_normalization_tolerance = 1e-12;
inline constexpr std::uint32_t restoration_minimum_guard = 32;
inline constexpr std::uint32_t restoration_guard_radius_factor = 4;
class Wiener {
  public:
    [[nodiscard]] static core::Result<Wiener> create(const WienerParameters& p = {});
    [[nodiscard]] const WienerParameters& parameters() const& noexcept {
        return parameters_;
    }
    [[nodiscard]] const WienerParameters& parameters() const&& = delete;
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return wiener_descriptor;
    }

  private:
    explicit Wiener(WienerParameters p) : parameters_(p) {}
    WienerParameters parameters_;
};
struct RestorationOff {};
using Restoration = std::variant<RestorationOff, Wiener>;
struct ResolvedPsf {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<double> coefficients;
    // Weighted pixel offsets from the central PSF origin; kernels are never recentered.
    double centroid_x = 0;
    double centroid_y = 0;
    std::optional<core::ContentIdentity> source_identity = std::nullopt;
    bool operator==(const ResolvedPsf&) const = default;
};
[[nodiscard]] core::Result<std::uint32_t> restoration_fft_size(std::uint64_t minimum);
[[nodiscard]] core::Result<std::uint32_t> psf_side(const Psf& psf);
[[nodiscard]] core::Result<void> gaussian_psf(GaussianPsf p, std::span<double> coefficients);
[[nodiscard]] core::Result<void> motion_psf(MotionPsf p, std::span<double> coefficients);
[[nodiscard]] core::Result<void> normalize_psf(std::uint32_t width, std::uint32_t height,
                                               std::span<double> coefficients);
[[nodiscard]] core::Result<std::array<double, 2>>
psf_centroid(std::uint32_t width, std::uint32_t height, std::span<const double> coefficients);
[[nodiscard]] core::Result<void> validate_psf(const ResolvedPsf& psf);
[[nodiscard]] bool canonical_psf(const WienerParameters& p, const ResolvedPsf& psf);
// Input is admitted finite Unit; candidate is admitted finite and may be outside Unit.
[[nodiscard]] double restoration_target(double input, double candidate,
                                        const Wiener& method) noexcept;
} // namespace docenhance::methods
