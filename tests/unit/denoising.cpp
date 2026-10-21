// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/denoising.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/nlm_working.hpp"
#include "gamma_fixture.hpp"
#include "nlm_math.hpp"
#include "stub_verifier.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <stdexcept>
#include <utility>

namespace docenhance::tests {
namespace {
constexpr std::size_t denoise_budget = std::size_t{32} * 1024 * 1024;
class PeakProcessor final : public app::Processor {
  public:
    explicit PeakProcessor(std::uint64_t peak) : peak_(peak) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const app::ExecutionControl& /*control*/) override {
        return std::unexpected(app::ProcessFailure{
            .error = {.code = core::ErrorCode::resource, .message = "Allocation refused"},
            .diagnostics =
                app::Diagnostics{
                    .threads = 1,
                    .memory_limit_mib = core::default_memory_limit / (std::size_t{1024} * 1024),
                    .charged_peak_bytes = peak_,
                },
        });
    }

  private:
    std::uint64_t peak_;
};
} // namespace
TEST_CASE("D01 signed candidate transport and zero blend preserve exact entering values") {
    const image::Rgb between{0.123456789, 0.345678912, 0.789123456};
    const auto entering = image::luminance(between);
    const auto f = image::srgb_encode(entering);
    const auto q = methods::nlm_code(f);
    CHECK(methods::nlm_candidate(f, q, q) == f);
    constexpr double correction = 13;
    const auto expected = image::srgb_decode(f - (correction / 65535));
    const auto candidate = image::srgb_decode(methods::nlm_candidate(f, q, q - correction));
    const auto corrected = image::transport_luminance(between, candidate);
    REQUIRE(image::valid_unit_samples(corrected));
    CHECK(std::abs(image::luminance(corrected) - expected) < 1e-14);
    CHECK(methods::nlm_candidate(0, 0, UINT16_MAX) == 1);
    CHECK(methods::nlm_candidate(1, UINT16_MAX, 0) == 0);
    core::Budget owner{denoise_budget};
    auto luminance = image::Plane<double>::allocate(owner, 1, 1).value();
    std::ranges::fill(luminance.view().storage(), -3.0);
    luminance.view().row(0).front() = entering;
    const auto before = gamma_bits(luminance.view().storage());
    core::Budget scratch{0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto facts = methods::apply(methods::Nlm::create({.blend = 0}).value(),
                                      {luminance.view(), {}, scratch, scheduler});
    REQUIRE(facts);
    CHECK(facts->reason == methods::NlmReason::zero_blend);
    CHECK(facts->counts.evaluated == 0);
    CHECK(gamma_bits(luminance.view().storage()) == before);
    CHECK(scratch.used() == 0);
}
TEST_CASE("Native reservations refund, move and retain ledger lifetime") {
    core::Reservation held;
    constexpr std::size_t limit = 1000;
    {
        core::Budget budget{limit};
        auto reservation = budget.reserve(limit);
        REQUIRE(reservation);
        REQUIRE(budget.used() == limit);
        REQUIRE(!(budget.allocate(1)));
        held = std::move(*reservation);
        REQUIRE(reservation->size() == 0);
        REQUIRE(budget.used() == limit);
    }
    REQUIRE(held.size() == limit);
    held = {};
    REQUIRE(held.size() == 0);
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE("Current ledger ceiling and returned diagnostics retain their owning boundaries") {
    core::Budget budget{core::default_memory_limit};
    auto reserved = budget.reserve(core::default_memory_limit);
    REQUIRE(reserved);
    CHECK(budget.used() == core::default_memory_limit);
    const auto refused = budget.reserve(1);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::resource);
    CHECK(budget.used() == core::default_memory_limit);
    *reserved = {};
    CHECK(budget.used() == 0);
    app::Invocation request;
    request.command = app::Command::process;
    request.operands = {"input.png"};
    request.option(app::OptionId::out_dir) = "output";
    request.option(app::OptionId::denoise) = "nlm";
    REQUIRE(app::prepare_process(request));
    UnusedVerifier verifier;
    for (const auto excess : {0U, 1U}) {
        PeakProcessor processor{core::default_memory_limit + excess};
        const auto outcome = app::dispatch(request, processor, verifier);
        const auto& failure = std::get<app::Failure>(outcome.payload);
        CHECK(failure.error.code == core::ErrorCode::resource);
        CHECK(failure.publication == core::Publication::not_started);
        REQUIRE(failure.diagnostics.has_value());
        if (!failure.diagnostics.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        CHECK(failure.diagnostics.value().charged_peak_bytes ==
              core::default_memory_limit + excess);
        CHECK(failure.diagnostics.value().memory_limit_mib ==
              core::default_memory_limit / (std::size_t{1024} * 1024));
        CHECK(failure.diagnostics.value().threads == 1);
    }
}

} // namespace docenhance::tests
