// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"

#include <functional>
namespace docenhance::host {
struct ContinuousExecution {
    std::reference_wrapper<const core::Cancellation> cancellation;
    std::reference_wrapper<const bundle::RunContext> context;
};
[[nodiscard]] core::Result<app::PublishedContinuous>
continuous(const app::ProcessRequest& request, image::Continuous operation,
           ContinuousExecution execution, methods::IlluminationReport& illumination,
           methods::DenoisingReport& denoising);
} // namespace docenhance::host
