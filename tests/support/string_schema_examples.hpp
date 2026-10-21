// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/record/json.hpp"
namespace docenhance::tests {
[[nodiscard]] core::Result<void> string_schema_examples(record::Json& cases);
} // namespace docenhance::tests
