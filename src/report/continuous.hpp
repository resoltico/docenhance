// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace docenhance::report {
[[nodiscard]] nlohmann::ordered_json continuous_fields(const app::PublishedContinuous& value);
[[nodiscard]] std::string continuous_text(const app::PublishedContinuous& value);
} // namespace docenhance::report
