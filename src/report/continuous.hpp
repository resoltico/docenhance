// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace docenhance::report {
[[nodiscard]] nlohmann::ordered_json continuous_fields(const app::ContinuousProcessed& value);
[[nodiscard]] std::string continuous_text(const app::ContinuousProcessed& value);
} // namespace docenhance::report
