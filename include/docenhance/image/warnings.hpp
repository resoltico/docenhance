// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/warnings.hpp"
#include "docenhance/image/output.hpp"

namespace docenhance::image {
[[nodiscard]] core::WarningList warnings(const InterpretationReport& report) noexcept;
} // namespace docenhance::image
