// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/reviewed_methods.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
namespace docenhance::methods {
inline constexpr double wiener_default_k = 0.01;
inline constexpr double restoration_default_blend = 0.5;
inline constexpr double psf_off_center_limit = 0.25;
inline constexpr std::uint32_t psf_minimum_side = 3;
inline constexpr std::uint32_t psf_maximum_side = core::psf_dimension_max;
inline constexpr std::uint32_t fft_max_guard = 256;
inline constexpr std::uint32_t restoration_native_calls = 3;
inline constexpr std::uint32_t restoration_minimum_guard = 32;
inline constexpr std::uint32_t restoration_guard_radius_factor = 4;
inline constexpr std::uint64_t restoration_native_control = std::uint64_t{8} * 1024 * 1024;
inline constexpr std::uint64_t restoration_native_per_pixel = 40;
struct GaussianPsf {
    static constexpr double minimum_sigma = 0.3;
    static constexpr double maximum_sigma = 5;
    static constexpr std::uint32_t radius_sigmas = 3;
    double sigma = 1;
    bool operator==(const GaussianPsf&) const = default;
};
struct MotionPsf {
    static constexpr double minimum_length = 1;
    static constexpr double maximum_length = 31;
    static constexpr double minimum_angle = -180;
    static constexpr double maximum_angle = 180;
    static constexpr double default_length = 5;
    static constexpr std::uint32_t minimum_samples = 64;
    static constexpr std::uint32_t samples_per_pixel = 32;
    double length = default_length;
    double angle = 0;
    bool operator==(const MotionPsf&) const = default;
};
struct FilePsf {
    std::string path;
    bool operator==(const FilePsf&) const = default;
};
using Psf = std::variant<GaussianPsf, MotionPsf, FilePsf>;
struct WienerParameters {
    Psf psf = GaussianPsf{};
    double k = wiener_default_k;
    double blend = restoration_default_blend;
    bool operator==(const WienerParameters&) const = default;
};
class Wiener {
  public:
    static constexpr double minimum_k = 1e-5;
    static constexpr double maximum_k = 1;
    [[nodiscard]] static core::Result<Wiener> create(WienerParameters p = {});
    [[nodiscard]] const WienerParameters& parameters() const& noexcept {
        return parameters_;
    }
    [[nodiscard]] const WienerParameters& parameters() const&& = delete;
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return wiener_descriptor;
    }

  private:
    explicit Wiener(WienerParameters p) : parameters_(std::move(p)) {}
    WienerParameters parameters_;
};
struct RestorationOff {};
using Restoration = std::variant<RestorationOff, Wiener>;
struct PsfIdentity {
    std::string path;
    core::ContentIdentity identity;
    bool operator==(const PsfIdentity& other) const {
        return path == other.path && identity.sha256 == other.identity.sha256 &&
               identity.bytes == other.identity.bytes;
    }
};
struct ResolvedPsf {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<double> coefficients;
    // Weighted pixel offsets from the central PSF origin; kernels are never recentered.
    double centroid_x = 0;
    double centroid_y = 0;
    std::optional<PsfIdentity> source_identity = std::nullopt;
    bool operator==(const ResolvedPsf&) const = default;
};
enum class RestorationStatus { disabled, no_change, applied, failed };
enum class RestorationReason {
    none,
    zero_blend,
    no_eligible_samples,
    no_effect,
    processing_failure,
};
struct RestorationReport {
    RestorationStatus status = RestorationStatus::disabled;
    RestorationReason reason = RestorationReason::none;
    bool complete = false;
    std::optional<WienerParameters> requested = std::nullopt;
    std::optional<ResolvedPsf> psf = std::nullopt;
    std::uint32_t guard = 0;
    std::uint32_t fft_width = 0;
    std::uint32_t fft_height = 0;
    std::optional<double> padded_mean = std::nullopt;
    std::uint64_t eligible_samples = 0;
    std::uint64_t protected_samples = 0;
    std::uint64_t context_samples = 0;
    std::uint64_t evaluated_samples = 0;
    std::uint64_t corrected_samples = 0;
    std::uint64_t changed_samples = 0;
    std::uint64_t raw_low_samples = 0;
    std::uint64_t raw_high_samples = 0;
    std::uint64_t blended_low_samples = 0;
    std::uint64_t blended_high_samples = 0;
    std::uint64_t preparation_charge_peak = 0;
    std::uint64_t native_reserved_bytes = 0;
    std::uint32_t native_calls = 0;
    bool inference_warning = false;
    bool after_transform_warning = false;
    bool off_center_warning = false;
    bool operator==(const RestorationReport&) const = default;
};
struct RestorationCharge {
    std::uint64_t candidate = 0;
    std::uint64_t transfer = 0;
    std::uint64_t real_plane = 0;
    std::uint64_t complex_plane = 0;
    std::uint64_t native = 0;
    std::uint64_t peak = 0;
};
[[nodiscard]] core::Result<std::uint32_t> restoration_fft_size(std::uint64_t minimum);
[[nodiscard]] core::Result<RestorationCharge> restoration_charge(image::Extent extent,
                                                                 image::Extent fft);
[[nodiscard]] std::string_view status_name(RestorationStatus value) noexcept;
[[nodiscard]] std::string_view reason_name(RestorationReason value) noexcept;
[[nodiscard]] bool valid_restoration_paths(const Restoration& method) noexcept;
[[nodiscard]] bool valid_restoration_observations(const RestorationReport& report);
[[nodiscard]] bool valid_restoration_extent(const RestorationReport& report, image::Extent extent);
[[nodiscard]] bool valid_restoration(const RestorationReport& report, const Restoration& method);
[[nodiscard]] core::Result<std::uint32_t> psf_side(const Psf& psf);
[[nodiscard]] core::Result<void> gaussian_psf(GaussianPsf p, std::span<double> coefficients);
[[nodiscard]] core::Result<void> motion_psf(MotionPsf p, std::span<double> coefficients);
[[nodiscard]] core::Result<void> normalize_psf(std::uint32_t width, std::uint32_t height,
                                               std::span<double> coefficients);
[[nodiscard]] core::Result<std::array<double, 2>>
psf_centroid(std::uint32_t width, std::uint32_t height, std::span<const double> coefficients);
[[nodiscard]] core::Result<void> validate_psf(const ResolvedPsf& psf);
[[nodiscard]] bool canonical_psf(const WienerParameters& p, const ResolvedPsf& psf);
[[nodiscard]] core::Result<double> restoration_target(double input, double candidate,
                                                      const Wiener& method);
} // namespace docenhance::methods
