// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/stage_reports.hpp"

#include <optional>
#include <string>
namespace docenhance::report {
[[nodiscard]] core::Result<std::string>
denoising_text(const std::optional<methods::DenoisingFacts>& facts);
} // namespace docenhance::report
