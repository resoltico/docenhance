// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/working.hpp"

namespace docenhance::methods {
[[nodiscard]] core::Result<LevelsFacts> apply(const Levels& method, Working working);
} // namespace docenhance::methods
