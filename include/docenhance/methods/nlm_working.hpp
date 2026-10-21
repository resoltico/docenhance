// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/working.hpp"
namespace docenhance::methods {
[[nodiscard]] core::Result<NlmFacts> apply(const Nlm& method, Working working);
} // namespace docenhance::methods
