// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "admission.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
namespace docenhance::app {
[[nodiscard]] core::Result<methods::Plan> prepare_plan(const Invocation& invocation,
                                                       const SuppliedOptions& supplied);
} // namespace docenhance::app
