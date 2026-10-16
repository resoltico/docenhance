// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/process.hpp"

#include "admission.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "plan.hpp"

#include <expected>
#include <utility>
namespace docenhance::app {
core::Result<ProcessRequest> prepare_process(const Invocation& invocation) {
    if (invocation.command != Command::process) {
        return core::failure(core::ErrorCode::argument, "Expected a process invocation");
    }
    auto supplied = admit_process_options(invocation);
    if (!supplied) {
        return std::unexpected(std::move(supplied.error()));
    }
    if (invocation.operands.empty()) {
        return core::failure(core::ErrorCode::argument, "INPUT is required");
    }
    const auto& input = invocation.operands.front();
    const auto& output = invocation.option(OptionId::out_dir);
    if (!core::valid_path(input) || !output || !core::valid_path(*output)) {
        return core::failure(core::ErrorCode::argument,
                             "Input and output paths must be valid UTF-8 without NUL");
    }
    auto plan = prepare_plan(invocation, *supplied);
    if (!plan) {
        return std::unexpected(std::move(plan.error()));
    }
    return ProcessRequest{input, *output, std::move(*plan)};
}
} // namespace docenhance::app
