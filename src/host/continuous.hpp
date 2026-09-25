// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/methods/illumination.hpp"
namespace docenhance::host {
[[nodiscard]] core::Result<app::PublishedImage>
continuous(const app::ProcessRequest& request, image::Continuous operation,
           const core::Cancellation& cancellation, methods::IlluminationReport& illumination);
} // namespace docenhance::host
