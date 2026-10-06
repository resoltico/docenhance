// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "denoising.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/denoising.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
namespace docenhance::app {
namespace {
core::Result<std::uint32_t> window(const std::optional<std::string>& raw, std::uint32_t fallback) {
    if (!raw) {
        return fallback;
    }
    if (raw->empty() || !std::ranges::all_of(*raw, [](char c) { return c >= '0' && c <= '9'; })) {
        return core::failure(core::ErrorCode::argument,
                             "Denoising integer parameters require decimal digits");
    }
    std::uint32_t n = 0;
    const auto parsed =
        std::from_chars(std::to_address(raw->begin()), std::to_address(raw->end()), n);
    if (parsed.ec != std::errc{} || parsed.ptr != std::to_address(raw->end())) {
        return core::failure(core::ErrorCode::argument, "Denoising integer exceeds its range");
    }
    return n;
}
core::Result<methods::Denoising> tvl1(const contract::Invocation& invocation) {
    const methods::Tvl1Parameters defaults;
    auto lambda = invocation.tv_lambda
                      ? contract::parse_finite(*invocation.tv_lambda, methods::Tvl1::minimum_lambda,
                                               methods::Tvl1::maximum_lambda)
                      : core::Result<double>{defaults.lambda};
    auto iterations = window(invocation.tv_iterations, defaults.iterations);
    auto tolerance =
        invocation.tv_tolerance
            ? contract::parse_finite(*invocation.tv_tolerance, methods::Tvl1::minimum_tolerance,
                                     methods::Tvl1::maximum_tolerance)
            : core::Result<double>{defaults.tolerance};
    auto blend = invocation.denoise_blend ? contract::parse_finite(*invocation.denoise_blend, 0, 1)
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
    const methods::NlmParameters defaults;
    auto h = invocation.nlm_h
                 ? contract::parse_finite(*invocation.nlm_h, methods::nlm_min_h, methods::nlm_max_h)
                 : core::Result<double>{defaults.h};
    auto blend = invocation.denoise_blend ? contract::parse_finite(*invocation.denoise_blend, 0, 1)
                                          : core::Result<double>{defaults.blend};
    auto patch = window(invocation.nlm_patch, defaults.patch);
    auto search = window(invocation.nlm_search, defaults.search);
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
    return methods::Nlm::create({
                                    .h = *h,
                                    .patch = *patch,
                                    .search = *search,
                                    .blend = *blend,
                                })
        .transform([](auto method) -> methods::Denoising { return method; });
}
} // namespace docenhance::app
