// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/working.hpp"
namespace docenhance::methods {
[[nodiscard]] core::Result<WienerFacts> apply(const Wiener& method, const ResolvedPsf& psf,
                                              Working working);
} // namespace docenhance::methods
