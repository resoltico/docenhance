// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/pipeline/render.hpp"
#include "docenhance/record/record.hpp"

#include <functional>
#include <optional>
#include <string>
namespace docenhance::host {
// Concrete synchronous handoff. Every encoded owner stays live through writes and reconciliation.
struct RunPublication {
    std::reference_wrapper<const app::ProcessRequest> request;
    std::reference_wrapper<pipeline::Rendering> rendering;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const exec::Scheduler> scheduler;
    record::RunContext context;
    core::ContentIdentity source;
    std::string source_name;
    std::optional<core::ContentIdentity> mask;
    std::reference_wrapper<bool> publication_started;
};
[[nodiscard]] app::ProcessResult publish_run(RunPublication& run, app::ProcessFailure& failure);
} // namespace docenhance::host
