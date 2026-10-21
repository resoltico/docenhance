// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/process.hpp"

#include "admission.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/values.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "plan.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <utility>
namespace docenhance::app {
core::Result<ExecutionControl> ExecutionControl::create(std::optional<unsigned> workers,
                                                        std::uint64_t memory_bytes,
                                                        core::Cancellation cancellation) {
    constexpr auto mib = std::uint64_t{1024} * 1024;
    if ((workers &&
         (*workers < core::min_execution_threads || *workers > core::max_execution_threads)) ||
        memory_bytes < core::min_memory_limit_mib * mib ||
        memory_bytes > core::max_memory_limit_mib * mib || memory_bytes % mib != 0 ||
        memory_bytes > std::numeric_limits<std::size_t>::max()) {
        return core::failure(core::ErrorCode::argument,
                             "Execution settings outside their admitted domain");
    }
    ExecutionControl control;
    control.workers_ = workers;
    control.memory_limit_ = static_cast<std::size_t>(memory_bytes);
    control.cancellation_ = std::move(cancellation);
    return control;
}
core::Result<ExecutionControl> prepare_execution(const Invocation& invocation,
                                                 core::Cancellation cancellation) {
    if (invocation.command != Command::process) {
        return core::failure(core::ErrorCode::argument, "Execution settings require process");
    }
    const auto threads = effective_value(invocation, OptionId::threads);
    std::optional<unsigned> workers;
    if (threads != "auto") {
        auto value = parse_integer_option("--threads", threads, core::min_execution_threads,
                                          core::max_execution_threads);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        workers = *value;
    }
    auto memory =
        parse_integer_option("--memory-limit", effective_value(invocation, OptionId::memory_limit),
                             static_cast<std::uint32_t>(core::min_memory_limit_mib),
                             static_cast<std::uint32_t>(core::max_memory_limit_mib));
    if (!memory) {
        return std::unexpected(std::move(memory.error()));
    }
    constexpr auto mib = std::uint64_t{1024} * 1024;
    return ExecutionControl::create(workers, std::uint64_t{*memory} * mib, std::move(cancellation));
}
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
    ProcessRequest request{input, *output, invocation.option(OptionId::protect_mask),
                           invocation.option(OptionId::psf_file), *plan};
    if (!request.ready()) {
        return core::failure(core::ErrorCode::argument,
                             "Input resource roles do not match the plan");
    }
    return request;
}
} // namespace docenhance::app
