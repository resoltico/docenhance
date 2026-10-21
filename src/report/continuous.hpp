// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/report/warnings.hpp"

#include <string>
namespace docenhance::report {
[[nodiscard]] core::Result<std::string> continuous_text(const app::Published& value);
[[nodiscard]] core::Result<std::string> completed_text(const methods::StageReports& facts);
[[nodiscard]] core::Result<std::string> diagnostics_text(const app::Diagnostics& diagnostics);
[[nodiscard]] core::Result<std::string> warnings_text(const RunWarnings& warnings);
} // namespace docenhance::report
