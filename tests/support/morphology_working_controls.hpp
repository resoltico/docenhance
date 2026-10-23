// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/morphology_working.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "morphology_working_cases.hpp"
#include "morphology_working_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace docenhance::tests {
inline auto run_morphology(MorphologyWorkingFixture& fixture, methods::MorphologyParameters p,
                           const exec::Scheduler& scheduler) {
    return methods::apply(methods::Morphology::create(p).value(),
                          {fixture.luminance.view(), fixture.protection.view().as_const(),
                           fixture.budget, scheduler});
}
inline void morphology_resource_cases() {
    const auto peak = morphology_test_peak(21, 3, 1, 63);
    MorphologyWorkingFixture exact{21, 3, peak};
    const auto resident = exact.budget.used();
    const CheckpointStop observe{core::Checkpoint::allocation,
                                 std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler observed{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                   observe.cancellation()};
    const auto result = run_morphology(exact, {.radius = 1}, observed);
    require(result && CheckpointStop::visits() == 6 && exact.budget.peak() == resident + peak &&
                exact.budget.used() == resident,
            "six real analysis/A/B/queue/coefficients/statistics acquisitions earn exact padded "
            "peak/refund");
    MorphologyWorkingFixture refused{21, 3, peak - 1};
    const auto before = gamma_bits(refused.luminance.view().storage());
    const auto held = refused.budget.used();
    const CheckpointStop trace{core::Checkpoint::allocation,
                               std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler traced{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                 trace.cancellation()};
    const auto failure = run_morphology(refused, {.radius = 1}, traced);
    require(!failure && failure.error().code == core::ErrorCode::resource &&
                CheckpointStop::visits() == 4 && refused.budget.used() == held &&
                refused.budget.peak() ==
                    held + (2 * morphology_test_plane(21, 3, sizeof(double))) &&
                gamma_bits(refused.luminance.view().storage()) == before,
            "peak-minus-one actually reaches queue acquisition after two fields then refunds "
            "before map");
}
inline void morphology_noop_and_cold_cases() {
    MorphologyWorkingFixture fixture{3, 2, 0};
    check_morphology_reference(fixture, {.strength = 0, .max_gain = 1}, 7);
    check_morphology_reference(fixture, {.max_gain = 1}, 7);
    std::ranges::fill(fixture.protection.view().storage(), std::uint8_t{255});
    const auto zero = check_morphology_reference(fixture, {.strength = 0, .max_gain = 1}, 7);
    const auto unit = check_morphology_reference(fixture, {.max_gain = 1}, 7);
    const methods::MorphologyParameters defaults;
    const auto empty = check_morphology_reference(fixture, defaults, 7);
    require(zero.reason == methods::MorphologyReason::zero_strength &&
                unit.reason == methods::MorphologyReason::unit_gain &&
                empty.reason == methods::MorphologyReason::no_eligible_samples,
            "zero-strength then unit-gain then E0 owns exact no-work priority and metadata");
    const CheckpointStop stop{core::Checkpoint::measurement, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto invalid = methods::apply(methods::Morphology::create({.strength = 0}).value(),
                                        {{}, {}, fixture.budget, scheduler});
    require(!invalid && invalid.error().code == core::ErrorCode::argument &&
                CheckpointStop::visits() == 0,
            "real malformed Working admission outranks no-op and cancellation");
    const auto cancelled = run_morphology(fixture, {.strength = 0}, scheduler);
    require(!cancelled && cancelled.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 1,
            "valid algebraic no-op still observes actual eligibility cancellation");
    MorphologyWorkingFixture too_few{17, 1, 0};
    too_few.protection.view().row(0).front() = 255;
    too_few.protection.view().row(0).back() = 255;
    const exec::Scheduler healthy{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto inapplicable = run_morphology(too_few, defaults, healthy);
    require(!inapplicable && inapplicable.error().code == core::ErrorCode::method_inapplicable,
            "E15 on a17-pixel page refuses applicability before any workspace");
    for (const auto p : {
             methods::MorphologyParameters{.strength = -0.1},
             methods::MorphologyParameters{.max_gain = 4.1},
             methods::MorphologyParameters{.target = 0.01},
             methods::MorphologyParameters{.radius = 0},
         }) {
        require(!methods::Morphology::create(p),
                "actual factory keeps every primitive family admission");
    }
}
inline void morphology_cancel_case(core::Checkpoint phase, std::size_t allowed) {
    MorphologyWorkingFixture fixture{8, 4, morphology_test_scratch};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto held = fixture.budget.used();
    const CheckpointStop stop{phase, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_morphology(fixture, {.radius = 1}, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1 && fixture.budget.used() == held,
            "each actual requested phase/acquisition boundary is reached, joins and refunds "
            "without Facts");
    if (phase == core::Checkpoint::allocation || phase == core::Checkpoint::measurement) {
        require(gamma_bits(fixture.luminance.view().storage()) == before,
                "actual premap allocation/measurement stop leaves complete Y/padding unchanged");
    }
}
inline void morphology_phase_cancel_cases() {
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::measurement,
             core::Checkpoint::processing,
             core::Checkpoint::scheduling,
         }) {
        std::size_t visits = 0;
        {
            MorphologyWorkingFixture fixture{8, 4, morphology_test_scratch};
            const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
            const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                            observe.cancellation()};
            const auto result = run_morphology(fixture, {.radius = 1}, scheduler);
            require(result.has_value(),
                    "healthy counterpart calibrates actual phase visits without sleeps");
            visits = CheckpointStop::visits();
        }
        require(visits > 0, "each required phase is actually observed");
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            morphology_cancel_case(phase, allowed);
        }
    }
}
inline void morphology_map_stop_case(std::size_t allowed, bool protected_prefix) {
    MorphologyWorkingFixture fixture{2051, 1, morphology_test_scratch};
    std::ranges::fill(fixture.luminance.view().row(0), 0.25);
    fixture.luminance.view().row(0).back() = 0.1;
    if (protected_prefix) {
        std::ranges::fill(fixture.protection.view().row(0).first(1920), std::uint8_t{255});
    }
    const auto expected =
        morphology_working_reference(fixture, {.max_gain = 4, .target = 0.75, .radius = 1});
    require(expected.samples.at(2050) != fixture.luminance.view().row(0).back(),
            "independent late eligible target WOULD change without the requested map stop");
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto held = fixture.budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result =
        run_morphology(fixture, {.max_gain = 4, .target = 0.75, .radius = 1}, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1 && fixture.budget.used() == held,
            "actual post-fit map entry/interior stop returns no Facts and refunds every field");
    const auto after = gamma_bits(fixture.luminance.view().storage());
    if (allowed == 4154 || protected_prefix) {
        require(after == before, "map entry/protected prefix stops retain the entire Y backing");
    } else {
        require(
            !std::ranges::equal(std::span{after}.first(128), std::span{before}.first(128)) &&
                std::ranges::equal(std::span{after}.subspan(128), std::span{before}.subspan(128)),
            "actual map stores a changed permitted prefix and preserves unstarted suffix/padding");
    }
}
inline void morphology_negative_zero_case() {
    MorphologyWorkingFixture fixture{1, 1, morphology_test_scratch};
    fixture.luminance.view().row(0).front() = -0.0;
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = run_morphology(fixture, {.radius = 1}, scheduler);
    require(result && result->analysis, "actual negative-zero field returns complete analysis");
    if (!result || !result->analysis) {
        return;
    }
    require(std::bit_cast<std::uint64_t>(result->analysis->background_min) ==
                    std::bit_cast<std::uint64_t>(-0.0) &&
                std::bit_cast<std::uint64_t>(result->analysis->background_max) == 0 &&
                std::bit_cast<std::uint64_t>(fixture.luminance.view().row(0).front()) ==
                    std::bit_cast<std::uint64_t>(-0.0) &&
                result->counts.changed == 0,
            "literal min1/max+0 first-tie seeds and numeric-identity no-store preserve signed-zero "
            "bits");
}
inline void morphology_sample_cap_case() {
    constexpr std::size_t sample_bytes = std::size_t{8} * 1048576;
    MorphologyWorkingFixture fixture{1025, 1025, sample_bytes};
    for (std::uint32_t y = 0; y < 1025; ++y) {
        std::ranges::fill(fixture.luminance.view().row(y), 0.25);
    }
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto resident = fixture.budget.used();
    const CheckpointStop stop{core::Checkpoint::allocation, 1};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_morphology(fixture, {.radius = 1}, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 2 && fixture.budget.peak() == resident + sample_bytes &&
                fixture.budget.used() == resident &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "actual source sampling caps scratch at8MiB above one-million eligible pixels, then "
            "refunds before field");
}
inline void morphology_map_healthy_case(bool protected_prefix) {
    MorphologyWorkingFixture fixture{2051, 1, morphology_test_scratch};
    std::ranges::fill(fixture.luminance.view().row(0), 0.25);
    fixture.luminance.view().row(0).back() = 0.1;
    if (protected_prefix) {
        std::ranges::fill(fixture.protection.view().row(0).first(1920), std::uint8_t{255});
    }
    const auto resident = fixture.budget.used();
    const CheckpointStop observe{core::Checkpoint::processing,
                                 std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    observe.cancellation()};
    const auto result =
        run_morphology(fixture, {.max_gain = 4, .target = 0.75, .radius = 1}, scheduler);
    require(result && result->counts.changed > 0 && CheckpointStop::visits() == 4171 &&
                fixture.budget.used() == resident && fixture.luminance.view().row(0).back() != 0.1,
            "same actual wide healthy method calibrates processing phases and genuine final "
            "eligible write");
}
} // namespace docenhance::tests
