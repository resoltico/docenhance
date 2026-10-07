// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "sharpening.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <expected>
namespace docenhance::app {
core::Result<methods::Sharpening> prepare_sharpening(const contract::Invocation& i) {
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
    auto sigma = i.sharpen_sigma
                     ? contract::parse_finite(*i.sharpen_sigma, methods::Unsharp::minimum_sigma,
                                              methods::Unsharp::maximum_sigma)
                     : core::Result<double>{defaults.sigma};
    auto amount = i.sharpen_amount ? contract::parse_finite(*i.sharpen_amount, 0,
                                                            methods::Unsharp::maximum_amount)
                                   : core::Result<double>{defaults.amount};
    auto threshold =
        i.sharpen_threshold
            ? contract::parse_finite(*i.sharpen_threshold, 0, methods::Unsharp::maximum_threshold)
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
} // namespace docenhance::app
