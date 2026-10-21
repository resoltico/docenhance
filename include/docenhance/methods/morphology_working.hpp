// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/working.hpp"

namespace docenhance::methods {
[[nodiscard]] core::Result<MorphologyFacts> apply(const Morphology& method, Working working);
} // namespace docenhance::methods
