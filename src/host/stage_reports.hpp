// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
namespace docenhance::host {
void initialize_stages(const app::ProcessRequest& request,
                       methods::IlluminationReport& illumination,
                       methods::DenoisingReport& denoising, methods::ContrastReport& contrast,
                       methods::SharpenReport& sharpening);
}
