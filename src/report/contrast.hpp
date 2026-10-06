// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/contrast.hpp"

#include <string>
namespace docenhance::report {
[[nodiscard]] std::string contrast_text(const methods::ContrastReport& report);
}
