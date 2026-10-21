// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "binary_fixture.hpp"
#include "binary_participation.hpp"
#include "binary_reference.hpp"
#include "binary_resource_cases.hpp"
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "require.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>
namespace docenhance::tests {
template <typename Method>
void binary_worker_case(const Method& method, std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> expected;
    using Facts =
        decltype(methods::binarize(method, std::declval<methods::BinaryInput>()))::value_type;
    Facts expected_facts{};
    for (const auto workers : {1U, 2U, 7U}) {
        BinaryFixture fixture{width, height};
        const auto source = fixture.codes.view().storage();
        const std::vector<std::uint16_t> before(source.begin(), source.end());
        const auto held = fixture.budget.used();
        const auto concurrency = exec::Concurrency::resolve(workers, 7, 0, 0).value();
        constexpr std::uint32_t rows = 64;
        constexpr std::uint32_t columns = 4096;
        const auto bands = (height / rows) + static_cast<std::uint32_t>(height % rows != 0);
        const auto strips = (width / columns) + static_cast<std::uint32_t>(width % columns != 0);
        const auto items = std::is_same_v<Method, methods::Sauvola>
                               ? std::min(concurrency.workers(), strips)
                               : bands;
        const BinaryParticipation observe{concurrency.workers(), items};
        const exec::Scheduler scheduler{concurrency, observe.cancellation()};
        const auto result = methods::binarize(method, {
                                                          fixture.codes.view().as_const(),
                                                          fixture.out.view(),
                                                          fixture.budget,
                                                          scheduler,
                                                      });
        require(
            result && fixture.budget.used() == held && std::ranges::equal(source, before) &&
                BinaryParticipation::observed() >= 1 &&
                BinaryParticipation::observed() <= BinaryParticipation::active_bound() &&
                BinaryParticipation::caller_observed() ==
                    (BinaryParticipation::active_bound() == 1),
            "actual nonblocking participation follows A and joined ownership, not requested count");
        const auto output = fixture.out.view().storage();
        if (workers == 1) {
            expected.assign(output.begin(), output.end());
            expected_facts = *result;
        } else {
            require(std::ranges::equal(output, expected) && *result == expected_facts &&
                        result->black_samples == expected_facts.black_samples &&
                        result->white_samples ==
                            (std::uint64_t{width} * height) - expected_facts.black_samples,
                    "actual1/2/7 fixed partitions retain complete output and Facts exactly");
        }
        require(result->black_samples + result->white_samples == std::uint64_t{width} * height,
                "each complete typed worker Facts retains exact logical cardinality");
        binary_output_padding(fixture.out.view().as_const());
        std::cout << "Binary participation: requested=" << workers
                  << " admitted=" << scheduler.workers() << " items=" << items
                  << " A=" << BinaryParticipation::active_bound()
                  << " observed=" << BinaryParticipation::observed()
                  << " inline_caller=" << BinaryParticipation::caller_observed()
                  << " complete_typed_facts_equal=" << (*result == expected_facts) << "\n";
    }
}
template <typename Method>
std::size_t binary_phase_visits(const Method& method, core::Checkpoint phase) {
    BinaryFixture fixture{1031, 130};
    const CheckpointStop trace{phase, std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    trace.cancellation()};
    const auto result = methods::binarize(method, {
                                                      fixture.codes.view().as_const(),
                                                      fixture.out.view(),
                                                      fixture.budget,
                                                      scheduler,
                                                  });
    require(result.has_value(),
            "healthy actual binary counterpart calibrates reached phase visits");
    return CheckpointStop::visits();
}
template <typename Method>
void binary_phase_stop(const Method& method, core::Checkpoint phase, std::size_t allowed) {
    BinaryFixture fixture{1031, 130};
    const auto source = fixture.codes.view().storage();
    const std::vector<std::uint16_t> before(source.begin(), source.end());
    const auto held = fixture.budget.used();
    const CheckpointStop stop{phase, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = methods::binarize(method, {
                                                      fixture.codes.view().as_const(),
                                                      fixture.out.view(),
                                                      fixture.budget,
                                                      scheduler,
                                                  });
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1 && fixture.budget.used() == held &&
                std::ranges::equal(source, before),
            "actual attained phase cancellation returns no Facts and refunds joined owners");
    binary_output_padding(fixture.out.view().as_const());
    if (phase != core::Checkpoint::processing && phase != core::Checkpoint::scheduling) {
        require(std::ranges::all_of(fixture.out.view().storage(),
                                    [](auto value) { return value == binary_sentinel; }),
                "actual premap phase stop preserves every destination byte");
    }
}
template <typename Method> void binary_phase_cases(const Method& method) {
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::measurement,
             core::Checkpoint::solving,
             core::Checkpoint::scheduling,
             core::Checkpoint::processing,
         }) {
        const auto visits = binary_phase_visits(method, phase);
        std::size_t previous = std::numeric_limits<std::size_t>::max();
        if (visits == 0) {
            continue;
        }
        for (const auto allowed : {std::size_t{0}, visits / 2, visits - 1}) {
            if (allowed != previous) {
                binary_phase_stop(method, phase, allowed);
            }
            previous = allowed;
        }
    }
}
template <typename Method> void binary_prefix_case(const Method& method) {
    BinaryFixture fixture{1031, 130};
    const auto source = fixture.codes.view().storage();
    const std::vector<std::uint16_t> before(source.begin(), source.end());
    const auto held = fixture.budget.used();
    const exec::Scheduler healthy{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    require(methods::binarize(method,
                              {
                                  fixture.codes.view().as_const(),
                                  fixture.out.view(),
                                  fixture.budget,
                                  healthy,
                              })
                .has_value(),
            "actual healthy binary prefix counterpart completes");
    const auto complete = fixture.out.view().storage();
    const std::vector<std::uint8_t> expected(complete.begin(), complete.end());
    std::ranges::fill(fixture.out.view().storage(), binary_sentinel);
    const auto allowed = std::is_same_v<Method, methods::Sauvola> ? std::size_t{2} : std::size_t{1};
    const CheckpointStop stop{core::Checkpoint::processing, allowed};
    const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                  stop.cancellation()};
    const auto result = methods::binarize(
        method, {fixture.codes.view().as_const(), fixture.out.view(), fixture.budget, stopped});
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1 && fixture.budget.used() == held &&
                std::ranges::equal(source, before),
            "real mapping interior stop returns no Facts/refunds");
    for (std::size_t i = 0; i < complete.size(); ++i) {
        const auto actual = fixture.out.view().storage().subspan(i, 1).front();
        if (i < 1024) {
            require(expected.at(i) <= 1 && actual != binary_sentinel,
                    "every healthy and cancelled prefix sample was genuinely written logical0/1");
        }
        require(actual == (i < 1024 ? expected.at(i) : binary_sentinel),
                "actual mapped prefix and untouched suffix later rows/padding retain exact bytes");
    }
    require(expected.at(1024) != binary_sentinel && expected.back() == binary_sentinel &&
                expected.at(fixture.out.view().row_pitch()) != binary_sentinel,
            "healthy later logical pixels would change while all healthy padding stays entering");
}

struct BinaryControlReference {
    std::vector<std::uint8_t> output;
    std::uint64_t black = 0;
    BinaryOtsuReference otsu;
};
template <typename Method>
BinaryControlReference binary_control_reference(const BinaryFixture& fixture,
                                                const Method& method) {
    const auto codes = fixture.codes.view();
    const auto out = fixture.out.view();
    BinaryControlReference reference{};
    reference.output.assign(out.storage().size(), binary_sentinel);
    if constexpr (std::is_same_v<Method, methods::Otsu>) {
        reference.otsu = binary_otsu_reference(codes);
    }
    for (std::uint32_t y = 0; y < codes.height(); ++y) {
        for (std::uint32_t x = 0; x < codes.width(); ++x) {
            std::uint8_t expected = 0;
            if constexpr (std::is_same_v<Method, methods::Sauvola>) {
                expected = binary_sauvola_reference(codes, method, x, y);
            } else if constexpr (std::is_same_v<Method, methods::Otsu>) {
                expected = static_cast<std::uint8_t>(
                    binary_reference_bin(codes.row(y).subspan(x, 1).front()) >
                    reference.otsu.threshold);
            } else {
                expected = static_cast<std::uint8_t>(
                    static_cast<double>(codes.row(y).subspan(x, 1).front()) / binary_reference_max >
                    method.threshold());
            }
            reference.output.at((std::size_t{y} * out.row_pitch()) + x) = expected;
            reference.black += static_cast<std::uint64_t>(expected == 0);
        }
    }
    return reference;
}
template <typename Method>
std::size_t binary_control_healthy(const Method& method, BinaryFixture& fixture,
                                   const BinaryControlReference& reference) {
    const auto held = fixture.budget.used();
    const CheckpointStop trace{core::Checkpoint::processing,
                               std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    trace.cancellation()};
    const auto result = methods::binarize(
        method, {fixture.codes.view().as_const(), fixture.out.view(), fixture.budget, scheduler});
    require(
        result && std::ranges::equal(fixture.out.view().storage(), reference.output) &&
            result->black_samples == reference.black &&
            result->white_samples ==
                (std::uint64_t{fixture.codes.width()} * fixture.codes.height()) - reference.black &&
            fixture.budget.used() == held,
        "healthy complete backing and Facts match independently correct logical0/1 and padding17");
    if constexpr (std::is_same_v<Method, methods::Otsu>) {
        require(result->threshold_bin == reference.otsu.threshold &&
                    result->single_bin_fallback == reference.otsu.fallback,
                "healthy typed Otsu fit matches independent bin-merged fit");
    }
    require(fixture.out.view().row(fixture.out.height() - 1).back() <= 1,
            "actual LAST logical pixel was changed from entering17 before final fold");
    return CheckpointStop::visits();
}
inline void binary_written_region(const BinaryFixture& fixture,
                                  const BinaryControlReference& reference, std::size_t written) {
    const auto output = fixture.out.view();
    for (std::uint32_t y = 0; y < output.height(); ++y) {
        for (std::uint32_t x = 0; x < output.width(); ++x) {
            const auto index = (std::size_t{y} * output.row_pitch()) + x;
            const auto expected = reference.output.at(index);
            const auto actual = output.row(y).subspan(x, 1).front();
            require(expected <= 1 && actual == ((std::size_t{y} * output.width()) + x < written
                                                    ? expected
                                                    : binary_sentinel),
                    "every exact attained prefix sample is correct and later WOULD-change samples "
                    "stay17");
        }
    }
    binary_output_padding(output);
}
struct BinaryStopBoundary {
    std::size_t allowed;
    std::size_t written;
};
template <typename Method>
void binary_boundary_stop(const Method& method, BinaryFixture& fixture,
                          const BinaryControlReference& reference, BinaryStopBoundary boundary) {
    const auto source = fixture.codes.view().storage();
    const std::vector<std::uint16_t> before(source.begin(), source.end());
    const auto held = fixture.budget.used();
    std::ranges::fill(fixture.out.view().storage(), binary_sentinel);
    const CheckpointStop stop{core::Checkpoint::processing, boundary.allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = methods::binarize(
        method, {fixture.codes.view().as_const(), fixture.out.view(), fixture.budget, scheduler});
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == boundary.allowed + 1 && fixture.budget.used() == held &&
                std::ranges::equal(source, before),
            "actual exact band/fold stop returns no Facts and refunds joined owners with source "
            "intact");
    binary_written_region(fixture, reference, boundary.written);
    if (boundary.written == std::size_t{fixture.out.width()} * fixture.out.height()) {
        require(std::ranges::equal(fixture.out.view().storage(), reference.output),
                "final-fold stop follows every store including LAST logical pixel and full healthy "
                "backing");
    }
    std::cout << "Binary exact stop: method=" << Method::descriptor().selector
              << " allowed=" << boundary.allowed << " observed=" << CheckpointStop::visits()
              << " changed=" << boundary.written << " untouched="
              << (std::size_t{fixture.out.width()} * fixture.out.height()) - boundary.written
              << " no_facts=1 source_intact=1 refunded=1\n";
}
template <typename Method> void binary_boundary_cases(const Method& method) {
    BinaryFixture fixture{1031, 130};
    const auto source = fixture.codes.view().storage();
    const std::vector<std::uint16_t> before(source.begin(), source.end());
    const auto reference = binary_control_reference(fixture, method);
    const auto processing = binary_control_healthy(method, fixture, reference);
    require(std::ranges::equal(source, before), "healthy calibration preserves complete Q backing");
    if constexpr (!std::is_same_v<Method, methods::Sauvola>) {
        require(processing == 261, "actual healthy B03/Otsu processing total is261");
        binary_boundary_stop(method, fixture, reference, {.allowed = 128, .written = 64 * 1031});
        binary_boundary_stop(method, fixture, reference,
                             {.allowed = 257, .written = (128 * 1031) + 1024});
    }
    binary_boundary_stop(method, fixture, reference,
                         {.allowed = processing - 1, .written = 1031 * 130});
}
inline std::size_t binary_partial_output(const BinaryFixture& fixture,
                                         const BinaryControlReference& reference) {
    const auto output = fixture.out.view();
    std::size_t changed = 0;
    for (std::uint32_t y = 0; y < output.height(); ++y) {
        for (std::uint32_t x = 0; x < output.width(); ++x) {
            const auto actual = output.row(y).subspan(x, 1).front();
            const auto expected = reference.output.at((std::size_t{y} * output.row_pitch()) + x);
            require(
                expected <= 1 && (actual == binary_sentinel || actual == expected),
                "every parallel changed pixel independently correct and every untouched pixel17");
            changed += static_cast<std::size_t>(actual != binary_sentinel);
        }
    }
    require(changed != 0 && changed < std::size_t{output.width()} * output.height(),
            "actual multislot cancellation attained stores AND retained untouched logical pixels");
    binary_output_padding(output);
    return changed;
}
template <typename Method>
void binary_multislot_stop(const Method& method, BinaryFixture& fixture,
                           const BinaryControlReference& reference, unsigned workers) {
    const auto source = fixture.codes.view().storage();
    const std::vector<std::uint16_t> before(source.begin(), source.end());
    const auto held = fixture.budget.used();
    const auto items = std::is_same_v<Method, methods::Sauvola>
                           ? std::min(workers, (fixture.codes.width() + 4095) / 4096)
                           : (fixture.codes.height() + 63) / 64;
    const auto active = std::min(workers, items);
    core::Budget scratch{
        binary_expected_scratch(method, fixture.codes.width(), fixture.codes.height(), workers),
    };
    const CheckpointStop stop{core::Checkpoint::processing, std::size_t{2} * active};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = methods::binarize(
        method, {fixture.codes.view().as_const(), fixture.out.view(), scratch, scheduler});
    require(
        !result && result.error().code == core::ErrorCode::cancelled &&
            CheckpointStop::visits() >= (std::size_t{2} * active) + 1 &&
            fixture.budget.used() == held && scratch.used() == 0 &&
            std::ranges::equal(source, before),
        "actual configured2/7 cancellation joins and refunds simultaneous owners with no Facts");
    const auto changed = binary_partial_output(fixture, reference);
    std::cout << "Binary multislot stop: method=" << Method::descriptor().selector
              << " requested=" << workers << " admitted=" << scheduler.workers()
              << " items=" << items << " A=" << active << " observed=" << CheckpointStop::visits()
              << " changed=" << changed << " untouched="
              << (std::size_t{fixture.out.width()} * fixture.out.height()) - changed
              << " no_facts=1 source_intact=1 scratch_refunded=1 resident_held=" << held << "\n";
}
template <typename Method> void binary_multislot_cases(const Method& method) {
    for (const auto workers : {2U, 7U}) {
        constexpr bool strips = std::is_same_v<Method, methods::Sauvola>;
        BinaryFixture fixture{strips ? 32777U : 1031U, strips ? 2U : 449U};
        const auto reference = binary_control_reference(fixture, method);
        static_cast<void>(binary_control_healthy(method, fixture, reference));
        std::ranges::fill(fixture.out.view().storage(), binary_sentinel);
        binary_multislot_stop(method, fixture, reference, workers);
    }
}
} // namespace docenhance::tests
