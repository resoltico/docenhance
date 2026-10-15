// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
namespace docenhance::cli {
[[nodiscard]] app::Outcome allocation_failure(const app::Invocation& invocation) noexcept;
} // namespace docenhance::cli
