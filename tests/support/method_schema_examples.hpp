// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/record/json.hpp"

namespace docenhance::tests {
[[nodiscard]] core::Result<void> contrast_schema_examples(record::Json& result);
[[nodiscard]] core::Result<void> method_schema_examples(record::Json& result);
} // namespace docenhance::tests
