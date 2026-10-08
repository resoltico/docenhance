// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/sharpening.hpp"
namespace docenhance::app {
class ProcessRequest;
[[nodiscard]] core::Result<methods::Sharpening>
prepare_sharpening(const contract::Invocation& invocation);
[[nodiscard]] bool partial_sharpen(const methods::SharpenReport& report,
                                   const ProcessRequest& request);
} // namespace docenhance::app
