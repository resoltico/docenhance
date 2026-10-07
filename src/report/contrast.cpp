// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast.hpp"

#include "docenhance/methods/contrast.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <variant>
namespace docenhance::report {
std::string contrast_text(const methods::ContrastReport& r) {
    std::string text = "Contrast: " + std::string(methods::status_name(r.status)) + " (" +
                       std::string(methods::reason_name(r.reason)) +
                       "); changed=" + std::to_string(r.changed_samples) + "\n";
    if (r.levels) {
        text += "Levels: lo=" + nlohmann::ordered_json(r.levels->low).dump() +
                "; hi=" + nlohmann::ordered_json(r.levels->high).dump() +
                "; clipped low=" + std::to_string(r.clipped_low_samples) +
                "; clipped high=" + std::to_string(r.clipped_high_samples) + "\n";
    }
    if (r.requested && std::holds_alternative<methods::ClaheParameters>(*r.requested)) {
        text += "CLAHE: identity tiles=" + std::to_string(r.identity_tiles) + "\n";
    }
    return text;
}
} // namespace docenhance::report
