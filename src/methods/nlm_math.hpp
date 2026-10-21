// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
namespace docenhance::methods {
// Admitted finite Unit perceptual value; code/mean are finite prepared NLM values.
[[nodiscard]] std::uint16_t nlm_code(double perceptual) noexcept;
[[nodiscard]] double nlm_candidate(double perceptual, std::uint16_t code, double mean) noexcept;
// The table producer owns positive finite normalization and d<Dc.
[[nodiscard]] double nlm_weight(std::uint32_t distance, double normalization) noexcept;
} // namespace docenhance::methods
