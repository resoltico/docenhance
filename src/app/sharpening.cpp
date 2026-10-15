// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "sharpening.hpp"

#include "docenhance/app/invocation.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/values.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <expected>
#include <variant>
namespace docenhance::app {
core::Result<methods::Sharpening> prepare_sharpening(const app::Invocation& i) {
    const bool parameters = i.sharpen_sigma || i.sharpen_amount || i.sharpen_threshold;
    if (i.output_mode == "bw" && (i.sharpen || parameters)) {
        return core::failure(core::ErrorCode::argument, "Sharpening requires continuous output");
    }
    const auto mode = i.sharpen.value_or("off");
    if (mode == "off") {
        if (parameters) {
            return core::failure(core::ErrorCode::argument,
                                 "Sharpening parameters require --sharpen unsharp");
        }
        return methods::SharpenOff{};
    }
    if (mode != "unsharp") {
        return core::failure(core::ErrorCode::argument, "--sharpen requires off or unsharp");
    }
    const methods::UnsharpParameters defaults;
    auto sigma = i.sharpen_sigma ? app::parse_decimal_option("--sharpen-sigma", *i.sharpen_sigma,
                                                             methods::Unsharp::minimum_sigma,
                                                             methods::Unsharp::maximum_sigma)
                                 : core::Result<double>{defaults.sigma};
    auto amount = i.sharpen_amount
                      ? app::parse_decimal_option("--sharpen-amount", *i.sharpen_amount, 0,
                                                  methods::Unsharp::maximum_amount)
                      : core::Result<double>{defaults.amount};
    auto threshold = i.sharpen_threshold
                         ? app::parse_decimal_option("--sharpen-threshold", *i.sharpen_threshold, 0,
                                                     methods::Unsharp::maximum_threshold)
                         : core::Result<double>{defaults.threshold};
    if (!sigma) {
        return std::unexpected(sigma.error());
    }
    if (!amount) {
        return std::unexpected(amount.error());
    }
    if (!threshold) {
        return std::unexpected(threshold.error());
    }
    return methods::Unsharp::create({.sigma = *sigma, .amount = *amount, .threshold = *threshold})
        .transform([](auto value) -> methods::Sharpening { return value; });
}
bool partial_sharpen(const methods::SharpenReport& r, const ProcessRequest& request) {
    const auto* const method = std::get_if<methods::Unsharp>(
        &std::get<app::ContinuousPlan>(request.plan()).photometric.sharpening);
    if (method != nullptr ? (!r.requested || *r.requested != method->parameters())
                          : r.requested.has_value()) {
        return false;
    }
    if (!methods::valid_sharpen_observations(r)) {
        return false;
    }
    return r.complete()
               ? methods::valid_sharpen(
                     r, std::get<app::ContinuousPlan>(request.plan()).photometric.sharpening)
               : methods::valid_progress(r);
}
} // namespace docenhance::app
