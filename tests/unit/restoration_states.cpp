// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/restoration.hpp"
#include "stub_verifier.hpp"

#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <utility>
namespace docenhance::tests {
namespace {
class ReturningProcessor final : public app::Processor {
  public:
    explicit ReturningProcessor(app::ProcessResult value) : value_(std::move(value)) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        ++calls_;
        return std::move(value_);
    }
    [[nodiscard]] unsigned calls() const {
        return calls_;
    }

  private:
    app::ProcessResult value_;
    unsigned calls_ = 0;
};
contract::Invocation invocation() {
    contract::Invocation value;
    value.command = contract::Command::process;
    value.subject = "input.png";
    value.output_directory = "output";
    return value;
}
const core::Error& error(const app::Outcome& value) {
    return std::get<app::ProcessFailure>(value.payload).error;
}
} // namespace
TEST_CASE("Restoration partial failures preserve bounded observations and reject contradictions",
          "[app][restoration]") {
    UnusedVerifier verifier;
    auto request = invocation();
    request.deblur = "wiener";
    request.psf = "gaussian";
    methods::RestorationReport known{
        .status = methods::RestorationStatus::failed,
        .reason = methods::RestorationReason::processing_failure,
        .inference_warning = true,
    };
    known.requested = methods::WienerParameters{};
    for (unsigned change = 0; change < 5; ++change) {
        auto observed = known;
        REQUIRE(observed.requested);
        if (change == 1) {
            observed.requested.value().k = 0.02;
        } else if (change == 2) {
            observed.inference_warning = false;
        } else if (change == 3) {
            observed.evaluated_samples = 1;
        } else if (change == 4) {
            observed.complete = true;
        }
        ReturningProcessor processor{std::unexpected(app::ProcessFailure{
            .error = {.code = core::ErrorCode::resource, .message = "Restoration refused"},
            .restoration = observed,
        })};
        const auto outcome = app::dispatch(request, processor, verifier);
        CHECK(processor.calls() == 1);
        CHECK(error(outcome).code ==
              (change == 0 ? core::ErrorCode::resource : core::ErrorCode::publication_unknown));
        if (change == 0) {
            CHECK(std::get<app::ProcessFailure>(outcome.payload).restoration == observed);
        }
    }
}
} // namespace docenhance::tests
