// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/working.hpp"

namespace docenhance::methods {
[[nodiscard]] core::Result<UnsharpFacts> apply(const Unsharp& method, Working working);
} // namespace docenhance::methods
