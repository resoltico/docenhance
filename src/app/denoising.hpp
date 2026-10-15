// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/invocation.hpp"
#include "docenhance/methods/denoising.hpp"
namespace docenhance::app {
[[nodiscard]] core::Result<methods::Denoising> prepare_denoising(const app::Invocation& invocation);
}
