// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/methods/illumination.hpp"

#include <nlohmann/json.hpp>
#include <string>
namespace docenhance::report {
[[nodiscard]] nlohmann::ordered_json illumination_fields(const methods::IlluminationReport& report);
[[nodiscard]] std::string illumination_text(const methods::IlluminationReport& report);
} // namespace docenhance::report
