// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/surface_facts.hpp"
#include "illumination_fact_checks.hpp"

namespace docenhance::methods {
[[nodiscard]] IlluminationApplicationFacts surface_application(const SurfaceFacts& facts) noexcept;
} // namespace docenhance::methods
