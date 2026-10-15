// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "denoising.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stages.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "docenhance/opencv/restoration.hpp"
#include "linear_fixture.hpp"
#include "stub_verifier.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
namespace docenhance::tests {
namespace {
enum class Family { illumination, nlm, tvl1, restoration, gamma, sharpening };
constexpr image::Extent model_extent{.width = 32, .height = 32};
constexpr std::uint64_t model_pixels = std::uint64_t{model_extent.width} * model_extent.height;
constexpr std::size_t model_budget = std::size_t{16} * 1024 * 1024;
app::Invocation model_request(Family family) {
    app::Invocation request;
    request.command = app::Command::process;
    request.operands = {"input.png"};
    request.option(app::OptionId::out_dir) = "output";
    switch (family) {
    case Family::illumination:
        request.option(app::OptionId::illumination) = "surface";
        request.option(app::OptionId::background_target) = "1";
        break;
    case Family::nlm:
        request.option(app::OptionId::denoise) = "nlm";
        break;
    case Family::tvl1:
        request.option(app::OptionId::denoise) = "tvl1";
        break;
    case Family::restoration:
        request.option(app::OptionId::deblur) = "wiener";
        request.option(app::OptionId::psf) = "gaussian";
        break;
    case Family::gamma:
        request.option(app::OptionId::contrast) = "gamma";
        request.option(app::OptionId::gamma) = "2";
        break;
    case Family::sharpening:
        request.option(app::OptionId::sharpen) = "unsharp";
        break;
    }
    return request;
}
template <class Report> void complete_off(Report& report) {
    if (!report.requested) {
        report.progress = methods::StageProgress::completed;
        report.eligible_samples = model_pixels;
    }
}
void require_success(bool value) {
    REQUIRE(value);
}
methods::StageObservations prepare_model(Family family, const app::ProcessRequest& request) {
    const auto& plan = std::get<app::ContinuousPlan>(request.plan()).photometric;
    auto stages = app::initial_stages(plan);
    // Every earlier disabled decision completed before this owner's model became usable.
    complete_off(stages.illumination);
    if (family != Family::nlm && family != Family::tvl1) {
        complete_off(stages.denoising);
    }
    if (family == Family::illumination || family == Family::gamma || family == Family::sharpening) {
        complete_off(stages.restoration);
    }
    if (family == Family::illumination || family == Family::sharpening) {
        complete_off(stages.contrast);
    }
    if (family == Family::illumination) {
        complete_off(stages.sharpening);
    }
    core::Budget budget{model_budget};
    LinearFixture source{budget, model_extent};
    source.fill(0.5);
    const auto held = budget.used();
    switch (family) {
    case Family::illumination: {
        const auto model = methods::SurfaceModel::prepare(
            {source, {}}, std::get<methods::Surface>(plan.illumination), budget, {},
            stages.illumination);
        require_success(model.has_value() && model->active());
        break;
    }
    case Family::nlm: {
        stages.denoising.progress = methods::StageProgress::preparing;
        stages.denoising.eligible_samples = model_pixels;
        const auto planes =
            host::prepare_denoising({source, {}}, request, budget, {}, stages.denoising);
        require_success(planes.has_value() && host::active_denoising(*planes));
        break;
    }
    case Family::tvl1: {
        const auto model = methods::Tvl1Model::prepare(
            source, {}, std::get<methods::Tvl1>(plan.denoising), {budget, {}, stages.denoising});
        require_success(model.has_value() && model->active());
        break;
    }
    case Family::restoration: {
        const auto& method = std::get<methods::Wiener>(plan.restoration);
        auto psf = opencv::resolve_psf(method);
        require_success(psf.has_value());
        const auto model = opencv::RestorationModel::prepare(
            source, {}, method, std::move(*psf),
            {budget, {}, stages.restoration, image::RowUse::output});
        require_success(model.has_value() && model->active());
        break;
    }
    case Family::gamma: {
        const auto model = methods::ContrastModel::prepare(
            source, {}, plan.contrast, {budget, {}, stages.contrast, image::RowUse::output});
        require_success(model.has_value() && model->active());
        break;
    }
    case Family::sharpening: {
        const auto model =
            methods::SharpenModel::prepare(source, {}, std::get<methods::Unsharp>(plan.sharpening),
                                           {budget, {}, stages.sharpening, image::RowUse::output});
        require_success(model.has_value() && model->active());
        break;
    }
    }
    CHECK(budget.used() == held);
    return stages;
}
template <class Action>
void selected_report(Family family, methods::StageObservations& stages, Action action) {
    switch (family) {
    case Family::illumination:
        action(stages.illumination);
        break;
    case Family::nlm:
    case Family::tvl1:
        action(stages.denoising);
        break;
    case Family::restoration:
        action(stages.restoration);
        break;
    case Family::gamma:
        action(stages.contrast);
        break;
    case Family::sharpening:
        action(stages.sharpening);
        break;
    }
}
class PreparedFailure final : public app::Processor {
  public:
    explicit PreparedFailure(methods::StageObservations reports) : reports_(std::move(reports)) {}
    [[nodiscard]] unsigned calls() const noexcept {
        return calls_;
    }
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        ++calls_;
        return app::process_failure(
            {
                .code = core::ErrorCode::resource,
                .message = "First observation block allocation refused",
            },
            reports_);
    }

  private:
    methods::StageObservations reports_;
    unsigned calls_ = 0;
};
template <class Report> void observe_sample(Report& report) {
    report.evaluated_samples = 1;
    if constexpr (std::is_same_v<Report, methods::IlluminationReport>) {
        report.min_gain = report.max_gain = 2;
        report.gain_capped_samples = report.changed_samples = 1;
    }
    if constexpr (std::is_same_v<Report, methods::ContrastReport>) {
        report.corrected_samples = report.changed_samples = 1;
    }
    if constexpr (std::is_same_v<Report, methods::SharpenReport>) {
        report.pre_clamp = methods::ExcursionRange{.low = 0.5, .high = 0.5};
    }
}
template <class Report> void invent_failure_reason(Report& report) {
    report.status = decltype(report.status)::failed;
    if constexpr (std::is_same_v<Report, methods::IlluminationReport>) {
        report.reason = methods::IlluminationReason::zero_strength;
    } else if constexpr (std::is_same_v<Report, methods::ContrastReport>) {
        report.reason = methods::ContrastReason::identity_gamma;
    } else if constexpr (std::is_same_v<Report, methods::SharpenReport>) {
        report.reason = methods::SharpenReason::zero_amount;
    } else {
        report.reason = decltype(report.reason)::zero_blend;
    }
}
template <class Report> void prepare_failure(Report& report, Family family) {
    CHECK(report.progress == methods::StageProgress::prepared);
    CHECK(report.evaluated_samples == 0);
    if (family != Family::illumination) {
        report.status = decltype(report.status)::failed;
        report.reason = decltype(report.reason)::processing_failure;
    }
}
template <class Report> void mutate(Report& report, unsigned mutation) {
    if (mutation == 1) {
        observe_sample(report);
    }
    if (mutation >= 2) {
        invent_failure_reason(report);
    }
    if (mutation == 3) {
        report.progress = methods::StageProgress::evaluating;
    }
}
void check_port(const app::Invocation& invocation, methods::StageObservations observed,
                bool malformed) {
    UnusedVerifier verifier;
    PreparedFailure processor{std::move(observed)};
    const auto result = app::dispatch(invocation, processor, verifier);
    const auto& error = std::get<app::Failure>(result.payload).error;
    CHECK(error.code ==
          (malformed ? core::ErrorCode::publication_unknown : core::ErrorCode::resource));
    CHECK(error.publication ==
          (malformed ? core::Publication::unknown : core::Publication::not_started));
    CHECK(processor.calls() == 1);
}
} // namespace
TEST_CASE("Actual prepared models retain zero application until their caller observes samples",
          "[app][host]") {
    for (const auto family : {
             Family::illumination,
             Family::nlm,
             Family::tvl1,
             Family::restoration,
             Family::gamma,
             Family::sharpening,
         }) {
        const auto invocation = model_request(family);
        const auto request = app::prepare_process(invocation);
        require_success(request.has_value());
        auto stages = prepare_model(family, *request);
        selected_report(family, stages,
                        [family](auto& report) { prepare_failure(report, family); });
        constexpr unsigned mutations = 4;
        for (unsigned mutation = 0; mutation < mutations; ++mutation) {
            auto observed = stages;
            selected_report(family, observed,
                            [mutation](auto& report) { mutate(report, mutation); });
            check_port(invocation, std::move(observed), mutation != 0);
        }
    }
}
TEST_CASE("Prepared illumination cannot claim a gain range before observing a sample",
          "[app][host]") {
    const auto invocation = model_request(Family::illumination);
    const auto request = app::prepare_process(invocation);
    require_success(request.has_value());
    auto stages = prepare_model(Family::illumination, *request);
    stages.illumination.min_gain = stages.illumination.max_gain = 2;
    CHECK(stages.illumination.evaluated_samples == 0);
    check_port(invocation, std::move(stages), true);
}
} // namespace docenhance::tests
