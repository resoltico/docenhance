// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "restoration.hpp"

#include "docenhance/methods/restoration.hpp"

#include <string>
namespace docenhance::report {
std::string restoration_text(const methods::RestorationReport& r) {
    if (!r.requested) {
        return {};
    }
    std::string text = "Restoration: " + std::string(methods::status_name(r.status)) + " (" +
                       std::string(methods::reason_name(r.reason)) + ")\n";
    if (r.inference_warning) {
        text += "W_RESTORATION_INFERENCE: known-PSF restoration is an inference; the actual "
                "camera blur and original document meaning are not verified.\n";
    }
    if (r.after_transform_warning) {
        text +=
            "W_PSF_AFTER_TRANSFORM: orientation, illumination or denoising precedes restoration; "
            "the assumed spatially invariant PSF may be approximate.\n";
    }
    if (r.off_center_warning) {
        text += "W_PSF_OFF_CENTER: the supplied kernel centroid is more than 0.25 pixels from "
                "its center; coefficients were not recentered.\n";
    }
    return text;
}
} // namespace docenhance::report
