// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/command.hpp"
namespace docenhance::cli {
[[nodiscard]] app::Outcome allocation_failure(const contract::Invocation& invocation) noexcept;
} // namespace docenhance::cli
