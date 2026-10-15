// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stages.hpp"
#include "docenhance/record/record.hpp"

#include <functional>
namespace docenhance::host {
struct ContinuousExecution {
    std::reference_wrapper<const core::Cancellation> cancellation;
    std::reference_wrapper<const record::RunContext> context;
};
[[nodiscard]] core::Result<app::PublishedContinuous>
continuous(const app::ProcessRequest& request, ContinuousExecution execution,
           methods::StageObservations& reports);
} // namespace docenhance::host
