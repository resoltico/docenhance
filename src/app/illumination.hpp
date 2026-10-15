// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/invocation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/illumination.hpp"
namespace docenhance::app {
[[nodiscard]] core::Result<methods::Illumination> prepare_illumination(const app::Invocation& v);
} // namespace docenhance::app
