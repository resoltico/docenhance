// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "restoration.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"

#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
namespace docenhance::app {
namespace {
core::Result<double> value(const std::optional<std::string>& raw, double fallback, double low,
                           double high) {
    return raw ? contract::parse_finite(*raw, low, high) : core::Result<double>{fallback};
}
core::Result<methods::Psf> psf(const contract::Invocation& i) {
    if (i.psf == "gaussian") {
        if (i.psf_length || i.psf_angle || i.psf_file) {
            return core::failure(core::ErrorCode::argument,
                                 "Gaussian PSF accepts only --psf-sigma");
        }
        auto sigma =
            value(i.psf_sigma, methods::GaussianPsf{}.sigma, methods::GaussianPsf::minimum_sigma,
                  methods::GaussianPsf::maximum_sigma);
        if (!sigma) {
            return std::unexpected(sigma.error());
        }
        return methods::GaussianPsf{.sigma = *sigma};
    }
    if (i.psf == "motion") {
        if (i.psf_sigma || i.psf_file) {
            return core::failure(core::ErrorCode::argument,
                                 "Motion PSF accepts only --psf-length and --psf-angle");
        }
        auto length = value(i.psf_length, methods::MotionPsf{}.length,
                            methods::MotionPsf::minimum_length, methods::MotionPsf::maximum_length);
        auto angle = value(i.psf_angle, methods::MotionPsf{}.angle,
                           methods::MotionPsf::minimum_angle, methods::MotionPsf::maximum_angle);
        if (!length) {
            return std::unexpected(length.error());
        }
        if (!angle) {
            return std::unexpected(angle.error());
        }
        return methods::MotionPsf{.length = *length, .angle = *angle};
    }
    if (i.psf == "kernel") {
        if (i.psf_sigma || i.psf_length || i.psf_angle || !i.psf_file ||
            !core::valid_path(*i.psf_file)) {
            return core::failure(core::ErrorCode::argument,
                                 "Kernel PSF requires a well-formed UTF-8 --psf-file path "
                                 "and accepts no Gaussian or motion parameters");
        }
        return methods::FilePsf{.path = *i.psf_file};
    }
    return core::failure(core::ErrorCode::argument,
                         "--deblur wiener requires --psf gaussian, motion or kernel");
}
} // namespace
core::Result<methods::Restoration> prepare_restoration(const contract::Invocation& i) {
    const bool parameters = i.psf || i.psf_sigma || i.psf_length || i.psf_angle || i.psf_file ||
                            i.wiener_k || i.deblur_blend;
    if (i.output_mode == "bw" && (i.deblur || parameters)) {
        return core::failure(core::ErrorCode::argument, "Restoration requires continuous output");
    }
    const auto mode = i.deblur.value_or("off");
    if (mode == "off") {
        if (parameters) {
            return core::failure(core::ErrorCode::argument,
                                 "Restoration parameters require --deblur wiener");
        }
        return methods::RestorationOff{};
    }
    if (mode != "wiener") {
        return core::failure(core::ErrorCode::argument, "--deblur requires off or wiener");
    }
    auto kernel = psf(i);
    auto k = value(i.wiener_k, methods::WienerParameters{}.k, methods::Wiener::minimum_k,
                   methods::Wiener::maximum_k);
    auto blend = value(i.deblur_blend, methods::WienerParameters{}.blend, 0, 1);
    if (!kernel) {
        return std::unexpected(kernel.error());
    }
    if (!k) {
        return std::unexpected(k.error());
    }
    if (!blend) {
        return std::unexpected(blend.error());
    }
    return methods::Wiener::create({.psf = std::move(*kernel), .k = *k, .blend = *blend})
        .transform([](auto selected) -> methods::Restoration { return selected; });
}
bool partial_restoration(const methods::RestorationReport& r, const ProcessRequest& request) {
    const auto* const method = std::get_if<methods::Wiener>(&request.restoration());
    if (method != nullptr ? (!r.requested || *r.requested != method->parameters())
                          : r.requested.has_value()) {
        return false;
    }
    if (!methods::valid_restoration_observations(r)) {
        return false;
    }
    return r.complete
               ? methods::valid_restoration(r, request.restoration())
               : (!r.requested || (r.status == methods::RestorationStatus::failed &&
                                   r.reason == methods::RestorationReason::processing_failure));
}
bool restoration_prefix(const methods::RestorationReport& r, image::Orientation orientation,
                        image::QuarterTurn rotation,
                        const methods::IlluminationReport& illumination,
                        const methods::DenoisingReport& denoising) {
    return r.after_transform_warning ==
           (r.requested.has_value() &&
            (orientation != image::Orientation::normal() || rotation.degrees() != 0 ||
             illumination.changed_samples != 0 || denoising.changed_samples != 0));
}
} // namespace docenhance::app
