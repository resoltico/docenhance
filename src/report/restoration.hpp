// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/restoration.hpp"

#include <string>
namespace docenhance::report {
[[nodiscard]] std::string restoration_text(const methods::RestorationReport& report);
}
