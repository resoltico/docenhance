// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/denoising.hpp"

#include <string>
namespace docenhance::report {
[[nodiscard]] std::string denoising_text(const methods::DenoisingReport& report);
}
