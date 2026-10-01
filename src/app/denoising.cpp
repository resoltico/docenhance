// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
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
        return core::failure(core::ErrorCode::argument, "NLM windows require decimal digits");
    }
    std::uint32_t n = 0;
    const auto parsed =
        std::from_chars(std::to_address(raw->begin()), std::to_address(raw->end()), n);
    if (parsed.ec != std::errc{} || parsed.ptr != std::to_address(raw->end())) {
        return core::failure(core::ErrorCode::argument, "NLM window exceeds integer range");
    }
    return n;
}
} // namespace
core::Result<methods::Denoising> prepare_denoising(const contract::Invocation& invocation) {
    const bool parameters = invocation.denoise_blend || invocation.nlm_h || invocation.nlm_patch ||
                            invocation.nlm_search;
    if (invocation.output_mode == "bw" && (invocation.denoise || parameters)) {
        return core::failure(core::ErrorCode::argument, "Denoising requires continuous output");
    }
    const auto mode = invocation.denoise.value_or("off");
    if (mode == "off") {
        if (parameters) {
            return core::failure(core::ErrorCode::argument, "NLM parameters require --denoise nlm");
        }
        return methods::DenoisingOff{};
    }
    if (mode != "nlm") {
        return core::failure(core::ErrorCode::argument, "--denoise requires off or nlm");
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
    return methods::Nlm::create({.h = *h, .patch = *patch, .search = *search, .blend = *blend})
        .transform([](auto method) -> methods::Denoising { return method; });
}
} // namespace docenhance::app
