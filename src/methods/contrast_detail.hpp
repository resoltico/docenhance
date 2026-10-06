// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/contrast.hpp"
namespace docenhance::methods {
[[nodiscard]] core::Result<LevelsRange> measure_levels(image::LinearSource& source,
                                                       image::PlaneView<const std::uint8_t> mask,
                                                       const Levels& method,
                                                       const ContrastExecution& execution);
}
