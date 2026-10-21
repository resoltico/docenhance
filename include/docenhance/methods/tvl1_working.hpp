// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/working.hpp"

#include <optional>

namespace docenhance::methods {
// Reset at entry; retains the actual solver stop through failure and successful Facts admission.
[[nodiscard]] core::Result<Tvl1Facts> apply(const Tvl1& method, Working working,
                                            std::optional<Tvl1Stop>& observed_stop);
} // namespace docenhance::methods
