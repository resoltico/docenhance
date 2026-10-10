// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "denoising.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/denoising.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
namespace docenhance::app {
namespace {
core::Result<methods::Denoising> tvl1(const contract::Invocation& invocation) {
    const methods::Tvl1Parameters defaults;
    auto lambda = invocation.tv_lambda
                      ? contract::parse_decimal_option("--tv-lambda", *invocation.tv_lambda,
                                                       methods::Tvl1::minimum_lambda,
                                                       methods::Tvl1::maximum_lambda)
                      : core::Result<double>{defaults.lambda};
    auto iterations =
        (invocation.tv_iterations
             ? contract::parse_integer_option("--tv-iterations", *invocation.tv_iterations,
                                              methods::Tvl1::minimum_iterations,
                                              methods::Tvl1::maximum_iterations)
             : core::Result<std::uint32_t>{defaults.iterations});
    auto tolerance = invocation.tv_tolerance
                         ? contract::parse_decimal_option(
                               "--tv-tolerance", *invocation.tv_tolerance,
                               methods::Tvl1::minimum_tolerance, methods::Tvl1::maximum_tolerance)
                         : core::Result<double>{defaults.tolerance};
    auto blend =
        invocation.denoise_blend
            ? contract::parse_decimal_option("--denoise-blend", *invocation.denoise_blend, 0, 1)
            : core::Result<double>{defaults.blend};
    if (!lambda) {
        return std::unexpected(lambda.error());
    }
    if (!iterations) {
        return std::unexpected(iterations.error());
    }
    if (!tolerance) {
        return std::unexpected(tolerance.error());
    }
    if (!blend) {
        return std::unexpected(blend.error());
    }
    return methods::Tvl1::create({
                                     .lambda = *lambda,
                                     .iterations = *iterations,
                                     .tolerance = *tolerance,
                                     .blend = *blend,
                                 })
        .transform([](auto method) -> methods::Denoising { return method; });
}
core::Result<methods::Denoising> nlm(const contract::Invocation& invocation) {
    const methods::NlmParameters defaults;
    auto h = invocation.nlm_h
                 ? contract::parse_decimal_option("--nlm-h", *invocation.nlm_h, methods::nlm_min_h,
                                                  methods::nlm_max_h)
                 : core::Result<double>{defaults.h};
    auto blend =
        invocation.denoise_blend
            ? contract::parse_decimal_option("--denoise-blend", *invocation.denoise_blend, 0, 1)
            : core::Result<double>{defaults.blend};
    auto patch =
        (invocation.nlm_patch
             ? contract::parse_integer_option("--nlm-patch", *invocation.nlm_patch,
                                              methods::nlm_min_patch, methods::nlm_max_patch, true)
             : core::Result<std::uint32_t>{defaults.patch});
    auto search = (invocation.nlm_search
                       ? contract::parse_integer_option("--nlm-search", *invocation.nlm_search,
                                                        methods::nlm_min_search,
                                                        methods::nlm_max_search, true)
                       : core::Result<std::uint32_t>{defaults.search});
    if (!h) {
        return std::unexpected(h.error());
    }
    if (!blend) {
        return std::unexpected(blend.error());
    }
    if (!patch) {
        return std::unexpected(patch.error());
    }
    if (!search) {
        return std::unexpected(search.error());
    }
    if (*search < *patch) {
        return std::unexpected(contract::option_error({
            .name = "--nlm-search",
            .value = invocation.nlm_search.value_or(std::to_string(*search)),
            .reason = "Must be at least --nlm-patch (" + std::to_string(*patch) + ")",
        }));
    }
    return methods::Nlm::create({
                                    .h = *h,
                                    .patch = *patch,
                                    .search = *search,
                                    .blend = *blend,
                                })
        .transform([](auto method) -> methods::Denoising { return method; });
}
} // namespace
core::Result<methods::Denoising> prepare_denoising(const contract::Invocation& invocation) {
    const bool parameters = invocation.denoise_blend || invocation.nlm_h || invocation.nlm_patch ||
                            invocation.nlm_search || invocation.tv_lambda ||
                            invocation.tv_iterations || invocation.tv_tolerance;
    if (invocation.output_mode == "bw" && (invocation.denoise || parameters)) {
        return core::failure(core::ErrorCode::argument, "Denoising requires continuous output");
    }
    const auto mode = invocation.denoise.value_or("off");
    if (mode == "off") {
        if (parameters) {
            return core::failure(core::ErrorCode::argument,
                                 "Denoising parameters require their selected method");
        }
        return methods::DenoisingOff{};
    }
    if (mode == "tvl1") {
        if (invocation.nlm_h || invocation.nlm_patch || invocation.nlm_search) {
            return core::failure(core::ErrorCode::argument, "NLM options require --denoise nlm");
        }
        return tvl1(invocation);
    }
    if (invocation.tv_lambda || invocation.tv_iterations || invocation.tv_tolerance) {
        return core::failure(core::ErrorCode::argument, "TV-L1 options require --denoise tvl1");
    }
    if (mode != "nlm") {
        return core::failure(core::ErrorCode::argument, "--denoise requires off, nlm or tvl1");
    }
    return nlm(invocation);
}
} // namespace docenhance::app
