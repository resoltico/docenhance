// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/illumination.hpp"
namespace docenhance::app {
[[nodiscard]] core::Result<methods::Illumination>
prepare_illumination(const contract::Invocation& v);
} // namespace docenhance::app
