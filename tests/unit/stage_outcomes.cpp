// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stages.hpp"

#include <catch2/catch_test_macros.hpp>

namespace docenhance::tests {
namespace {
app::Invocation selected() {
    app::Invocation v;
    v.command = app::Command::process;
    v.operands = {"absent-source.png"};
    v.option(app::OptionId::out_dir) = "unused-output";
    v.option(app::OptionId::illumination) = "surface";
    v.option(app::OptionId::denoise) = "nlm";
    v.option(app::OptionId::deblur) = "wiener";
    v.option(app::OptionId::psf) = "gaussian";
    v.option(app::OptionId::contrast) = "clahe";
    v.option(app::OptionId::sharpen) = "unsharp";
    return v;
}
} // namespace
TEST_CASE("Unstarted selected stages are distinct from a real source acquisition failure",
          "[host]") {
    const auto request = app::prepare_process(selected());
    REQUIRE(request);
    const auto& plan = std::get<app::ContinuousPlan>(request->plan());
    const auto stages = app::initial_stages(plan.photometric);
    host::Processor processor;
    const auto result = processor.process(*request, {});
    REQUIRE(!result);
    CHECK(result.error().error.code == core::ErrorCode::input);
    CHECK(result.error().error.publication == core::Publication::not_started);
    const auto& observed = result.error().stages;
    REQUIRE(observed.has_value());
    if (observed) {
        CHECK(observed->illumination == stages.illumination);
        CHECK(observed->denoising == stages.denoising);
        CHECK(observed->restoration == stages.restoration);
        CHECK(observed->contrast == stages.contrast);
        CHECK(observed->sharpening == stages.sharpening);
    }
}
} // namespace docenhance::tests
