// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/contrast.hpp"
namespace docenhance::app {
[[nodiscard]] core::Result<methods::Contrast>
prepare_contrast(const contract::Invocation& invocation);
}
