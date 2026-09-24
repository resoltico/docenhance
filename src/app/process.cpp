// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/process.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/contract/utf8.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/binarization.hpp"

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
core::Result<std::uint32_t> window_value(const std::optional<std::string>& value) {
    if (!value) {
        return methods::Sauvola::default_window;
    }
    std::uint32_t parsed = 0;
    if (value->empty() ||
        !std::ranges::all_of(*value, [](char byte) { return byte >= '0' && byte <= '9'; })) {
        return core::failure(core::ErrorCode::argument, "--sauvola-window requires decimal digits");
    }
    const auto conversion =
        std::from_chars(std::to_address(value->begin()), std::to_address(value->end()), parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != std::to_address(value->end())) {
        return core::failure(core::ErrorCode::argument,
                             "--sauvola-window is outside its integer range");
    }
    return parsed;
}
core::Result<double> value_or(const std::optional<std::string>& value, double fallback,
                              double low) {
    return value ? contract::parse_finite(*value, low, 1.0) : core::Result<double>{fallback};
}
core::Result<methods::Binarization> prepare_method(const contract::Invocation& invocation) {
    if (invocation.binarize == methods::FixedThreshold::descriptor().selector) {
        if (invocation.sauvola_window || invocation.sauvola_k || invocation.sauvola_r) {
            return core::failure(core::ErrorCode::argument,
                                 "Sauvola options require --binarize sauvola");
        }
        const auto threshold =
            value_or(invocation.fixed_threshold, methods::FixedThreshold::default_threshold, 0.0);
        if (!threshold) {
            return std::unexpected(threshold.error());
        }
        return methods::FixedThreshold::create(*threshold)
            .transform([](auto value) -> methods::Binarization { return value; });
    }
    if (invocation.binarize != methods::Sauvola::descriptor().selector) {
        return core::failure(core::ErrorCode::argument, "--binarize requires fixed or sauvola");
    }
    if (invocation.fixed_threshold) {
        return core::failure(core::ErrorCode::argument,
                             "--fixed-threshold requires --binarize fixed");
    }
    const auto window = window_value(invocation.sauvola_window);
    if (!window) {
        return std::unexpected(window.error());
    }
    const auto k = value_or(invocation.sauvola_k, methods::Sauvola::default_k, 0.0);
    if (!k) {
        return std::unexpected(k.error());
    }
    const auto r =
        value_or(invocation.sauvola_r, methods::Sauvola::default_r, methods::Sauvola::min_r);
    if (!r) {
        return std::unexpected(r.error());
    }
    return methods::Sauvola::create({.window = *window, .k = *k, .r = *r})
        .transform([](auto value) -> methods::Binarization { return value; });
}
} // namespace
core::Result<ProcessRequest> prepare_process(const contract::Invocation& invocation) {
    if (invocation.command != contract::Command::process) {
        return core::failure(core::ErrorCode::argument, "Expected a process invocation");
    }
    if (invocation.subject.empty()) {
        return core::failure(core::ErrorCode::argument, "INPUT is required");
    }
    if (invocation.output_directory.empty()) {
        return core::failure(core::ErrorCode::argument, "--out-dir is required");
    }
    if (invocation.subject.contains('\0') || invocation.output_directory.contains('\0')) {
        return core::failure(core::ErrorCode::argument, "Paths cannot contain NUL bytes");
    }
    if (!contract::valid_utf8(invocation.subject) ||
        !contract::valid_utf8(invocation.output_directory)) {
        return core::failure(core::ErrorCode::argument, "Paths must be well-formed UTF-8");
    }
    auto method = prepare_method(invocation);
    if (!method) {
        return std::unexpected(method.error());
    }
    return ProcessRequest{invocation.subject, invocation.output_directory, *method};
}
} // namespace docenhance::app
