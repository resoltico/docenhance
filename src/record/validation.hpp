// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/record/record.hpp"
namespace docenhance::record {
[[nodiscard]] bool source_agrees(const RunRecord& value);
[[nodiscard]] bool execution_agrees(const RunRecord& value);
[[nodiscard]] bool assets_agree(const RunRecord& value);
} // namespace docenhance::record
