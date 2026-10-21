// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/record/record.hpp"
#include "validation.hpp"

#include <variant>
namespace docenhance::record {
bool assets_agree(const RunRecord& value) {
    const ProtectionAsset* mask = nullptr;
    const PsfAsset* psf = nullptr;
    for (const auto& asset : value.assets) {
        if (asset.valueless_by_exception()) {
            return false;
        }
        if (const auto* const candidate = std::get_if<ProtectionAsset>(&asset)) {
            if (mask != nullptr || !validate(*candidate)) {
                return false;
            }
            mask = candidate;
        } else {
            const auto& psf_candidate = std::get<PsfAsset>(asset);
            if (psf != nullptr || !validate(psf_candidate)) {
                return false;
            }
            psf = &psf_candidate;
        }
    }
    if ((mask != nullptr) != value.request.protection.has_value()) {
        return false;
    }
    if (mask != nullptr) {
        const auto& report = value.execution.interpretation;
        const auto shape = image::oriented_shape(report.decoded, report.orientation);
        if (shape.width != mask->width || shape.height != mask->height) {
            return false;
        }
    }
    const bool kernel = value.request.restoration && std::holds_alternative<methods::Kernel>(
                                                         value.request.restoration->parameters.psf);
    return (psf != nullptr) == kernel;
}
} // namespace docenhance::record
