// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/illumination.hpp"

#include <string>
namespace docenhance::report {
[[nodiscard]] std::string illumination_text(const methods::IlluminationReport& report);
} // namespace docenhance::report
