// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"

#include <functional>
namespace docenhance::host {
struct ContinuousReports {
    std::reference_wrapper<methods::IlluminationReport> illumination;
    std::reference_wrapper<methods::DenoisingReport> denoising;
    std::reference_wrapper<methods::ContrastReport> contrast;
};
struct ContinuousExecution {
    std::reference_wrapper<const core::Cancellation> cancellation;
    std::reference_wrapper<const bundle::RunContext> context;
};
[[nodiscard]] core::Result<app::PublishedContinuous> continuous(const app::ProcessRequest& request,
                                                                image::Continuous operation,
                                                                ContinuousExecution execution,
                                                                ContinuousReports reports);
} // namespace docenhance::host
