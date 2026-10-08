// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
namespace docenhance::app {
class ProcessRequest;
[[nodiscard]] core::Result<methods::Restoration>
prepare_restoration(const contract::Invocation& invocation);
[[nodiscard]] bool partial_restoration(const methods::RestorationReport& report,
                                       const ProcessRequest& request);
[[nodiscard]] bool restoration_prefix(const methods::RestorationReport& report,
                                      image::Orientation orientation,
                                      const methods::IlluminationReport& illumination,
                                      const methods::DenoisingReport& denoising);
} // namespace docenhance::app
