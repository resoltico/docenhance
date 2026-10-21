// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/warnings.hpp"
#include "docenhance/record/record.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace docenhance::report {
// Run aggregation is separate from the retained four-slot per-report projections.
using RunWarnings =
    std::array<std::optional<core::Warning>, core::EnumNames<core::Warning>::list.size()>;
// Consume already admitted facts/context. No warning identity is stored in a run record.
[[nodiscard]] RunWarnings run_warnings(const record::RunRecord& value) noexcept;
[[nodiscard]] RunWarnings failure_warnings(const app::ProcessFailure& value) noexcept;
[[nodiscard]] std::optional<std::string_view> warning_explanation(core::Warning warning) noexcept;
} // namespace docenhance::report
