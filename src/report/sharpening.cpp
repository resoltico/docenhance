// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "sharpening.hpp"

#include "docenhance/methods/sharpening.hpp"

#include <string>
namespace docenhance::report {
std::string sharpening_text(const methods::SharpenReport& r) {
    if (!r.requested) {
        return {};
    }
    std::string text = "Sharpening: " + std::string(methods::status_name(r.status)) + " (" +
                       std::string(methods::reason_name(r.reason)) + ")\n";
    if (r.pre_clamp) {
        text += "Pre-clamp perceptual range: " + std::to_string(r.pre_clamp->low) + ".." +
                std::to_string(r.pre_clamp->high) + "\n";
    }
    if (r.requested->amount != 0) {
        text += "W_SHARPENING\n";
    }
    return text;
}
} // namespace docenhance::report
