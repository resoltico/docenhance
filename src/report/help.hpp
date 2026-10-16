// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/dispatch.hpp"

#include <span>
#include <string>
namespace docenhance::report {
[[nodiscard]] std::string help_text(const app::Outcome& outcome, const app::Help& help);
[[nodiscard]] std::string methods_text(std::span<const methods::ImplementedMethod> methods);
} // namespace docenhance::report
