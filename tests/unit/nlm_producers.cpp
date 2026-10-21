// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/working.hpp"
#include "gamma_fixture.hpp"
#include "nlm_detail.hpp"
#include "nlm_working_cases.hpp"
#include "nlm_working_fixture.hpp"
#include "require.hpp"
#include "sharpen_reference.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <thread>
namespace docenhance::tests {
namespace {
struct StorageGate {
    static inline std::atomic<unsigned> arrivals{0};
    static inline std::atomic<bool> aborted{false};
    static constexpr unsigned participants = 7;
    static void reset() {
        arrivals.store(0);
        aborted.store(false);
    }
    static void abort() {
        aborted.store(true);
        arrivals.store(participants);
        arrivals.notify_all();
    }
    static bool observe(core::Checkpoint phase) noexcept {
        if (phase != core::Checkpoint::initialization) {
            return aborted.load();
        }
        const auto arrival = arrivals.fetch_add(1);
        arrivals.notify_all();
        if (arrival < participants) {
            auto current = arrivals.load();
            while (current < participants && !aborted.load()) {
                arrivals.wait(current);
                current = arrivals.load();
            }
        }
        return aborted.load();
    }
};
bool overlap_band_storage(const methods::NlmContext& context) {
    std::atomic<bool> failed{false};
    std::array<std::jthread, StorageGate::participants> threads;
    const auto task = [&](std::size_t index) noexcept {
        try {
            const auto storage = methods::nlm_storage(
                context, methods::nlm_band(449, static_cast<std::uint32_t>(index)));
            if (!storage) {
                failed.store(true);
                StorageGate::abort();
            }
        } catch (...) {
            failed.store(true);
            StorageGate::abort();
        }
    };
    try {
        for (std::size_t index = 0; index < threads.size(); ++index) {
            threads.at(index) = std::jthread{task, index};
        }
    } catch (...) {
        StorageGate::abort();
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        throw;
    }
    for (auto& thread : threads) {
        thread.join();
    }
    return !failed.load();
}
void distance_cutoff_case(std::uint16_t code) {
    NlmWorkingFixture fixture{7, 7, nlm_working_test_scratch};
    for (std::uint32_t y = 0; y < 7; ++y) {
        std::ranges::fill(fixture.luminance.view().row(y), 0);
    }
    fixture.luminance.view().row(3).front() = sharpen_reference_srgb_decode(code / 65535.0);
    const auto method = methods::Nlm::create({.h = 0.1, .patch = 3, .search = 7}).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const methods::Working working{fixture.luminance.view(), fixture.protection.view().as_const(),
                                   fixture.budget, scheduler};
    const auto prepared = methods::prepare_nlm(method, working);
    require(prepared && prepared->weights.width() == 694 &&
                prepared->codes.view().row(3).front() == code,
            "actual shared producer earns exact Q and real minimum cutoff table");
    const auto weights = prepared->weights.view().row(0);
    for (std::size_t distance = 0; distance < weights.size(); ++distance) {
        const double t = static_cast<double>(distance) / (9 * 65535.0 * (0.1 / 255));
        require(std::bit_cast<std::uint64_t>(weights.subspan(distance, 1).front()) ==
                    std::bit_cast<std::uint64_t>(std::exp(-(t * t))),
                "every actual minimum-table entry matches independent literal expression bits");
    }
    const methods::NlmContext context{method, working, *prepared};
    constexpr methods::NlmBand band{.first = 3, .rows = 1};
    auto storage = methods::nlm_storage(context, band);
    require(storage.has_value(), "actual one-row band acquires all four real storage owners");
    constexpr methods::NlmOffset offset{.x = 3, .y = 3};
    require(methods::nlm_distances(context, band, offset, *storage).has_value() &&
                methods::nlm_accumulate(context, band, offset, *storage).has_value() &&
                storage->columns.view().row(0).front() == code,
            "actual uint32 horizontal/vertical windows attain exact cutoff-minus-one or cutoff "
            "distance");
    const auto denominator = storage->accumulation.view().row(1).front();
    if (code == 694) {
        require(denominator == 0, "actual D==Dc contributes zero without indexing the table");
    } else {
        const double t = code / (9 * 65535.0 * (0.1 / 255));
        require(std::bit_cast<std::uint64_t>(denominator) ==
                    std::bit_cast<std::uint64_t>(std::exp(-(t * t))),
                "actual D==Dc-1 contribution matches the independent literal double weight bits");
    }
}
std::size_t first_band_processing_visits() {
    NlmWorkingFixture fixture{7, 65, nlm_working_test_scratch};
    const auto method = methods::Nlm::create({.h = 1, .patch = 3, .search = 7}).value();
    const CheckpointStop trace{core::Checkpoint::processing,
                               std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    trace.cancellation()};
    const methods::Working working{fixture.luminance.view(), fixture.protection.view().as_const(),
                                   fixture.budget, scheduler};
    const auto prepared = methods::prepare_nlm(method, working);
    require(prepared.has_value(),
            "actual private preparation supports prefix checkpoint calibration");
    const methods::NlmContext context{method, working, *prepared};
    constexpr methods::NlmBand band{.first = 0, .rows = 64};
    auto storage = methods::nlm_storage(context, band);
    require(storage.has_value(), "actual first-band storage is acquired before calibration work");
    for (std::int32_t oy = -3; oy <= 3; ++oy) {
        for (std::int32_t ox = -3; ox <= 3; ++ox) {
            const methods::NlmOffset offset{.x = ox, .y = oy};
            require(
                methods::nlm_distances(context, band, offset, *storage).has_value() &&
                    methods::nlm_accumulate(context, band, offset, *storage).has_value(),
                "actual first-band distance/accumulation owners calibrate processing before map");
        }
    }
    return CheckpointStop::visits();
}
} // namespace
TEST_CASE(
    "Working NLM private exact distances and weight table preserve border and cutoff owners") {
    const auto assertions_before = assertion_counter().load();
    NlmWorkingFixture fixture{7, 7, nlm_working_test_scratch};
    constexpr std::array<std::uint16_t, 7> codes{0, 100, 1000, 0, 0, 0, 0};
    for (std::uint32_t y = 0; y < 7; ++y) {
        for (std::uint32_t x = 0; x < 7; ++x) {
            fixture.luminance.view().row(y).subspan(x, 1).front() =
                sharpen_reference_srgb_decode(codes.at(x) / 65535.0);
        }
    }
    const auto method = methods::Nlm::create({.h = 3, .patch = 3, .search = 7}).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const methods::Working working{fixture.luminance.view(), fixture.protection.view().as_const(),
                                   fixture.budget, scheduler};
    const auto held = fixture.budget.used();
    {
        const auto prepared = methods::prepare_nlm(method, working);
        require(prepared && prepared->codes.view().row(0).subspan(2, 1).front() == 1000,
                "real prepared code owner retains the admitted asymmetric border pattern");
        const methods::NlmContext context{method, working, *prepared};
        constexpr methods::NlmBand band{.first = 3, .rows = 1};
        auto storage = methods::nlm_storage(context, band);
        require(storage.has_value(),
                "real typed band owner is live during private distance control");
        constexpr methods::NlmOffset offset{.x = 1, .y = 0};
        require(methods::nlm_distances(context, band, offset, *storage).has_value() &&
                    methods::nlm_accumulate(context, band, offset, *storage).has_value() &&
                    storage->columns.view().row(0).front() == 3300,
                "actual independent source folds and exact uint32 sums give3300 rather than "
                "invalid5700");
    }
    require(fixture.budget.used() == held, "actual private producer/distance owners refund");
    distance_cutoff_case(693);
    distance_cutoff_case(694);
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working NLM simultaneous seven actual band storage owners earn their charged peak") {
    const auto assertions_before = assertion_counter().load();
    NlmWorkingFixture fixture{9, 449, nlm_working_test_scratch};
    const auto method = methods::Nlm::create({.h = 1, .patch = 3, .search = 7}).value();
    const exec::Scheduler preparation{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const methods::Working initial{fixture.luminance.view(), fixture.protection.view().as_const(),
                                   fixture.budget, preparation};
    const auto resident = fixture.budget.used();
    const auto before = gamma_bits(fixture.luminance.view().storage());
    {
        const auto prepared = methods::prepare_nlm(method, initial);
        require(prepared.has_value(),
                "actual shared Q/table/folds/observations are produced before band overlap");
        const auto shared = fixture.budget.used() - resident;
        StorageGate::reset();
        const core::Cancellation cancellation{{}, StorageGate::observe};
        const exec::Scheduler scheduler{exec::Concurrency::resolve(7, 7, 0, 0).value(),
                                        cancellation};
        const methods::Working working{fixture.luminance.view(),
                                       fixture.protection.view().as_const(), fixture.budget,
                                       scheduler};
        const methods::NlmContext context{method, working, *prepared};
        const bool succeeded = overlap_band_storage(context);
        require(succeeded && StorageGate::arrivals.load() >= 7 && !StorageGate::aborted.load() &&
                    fixture.budget.used() == resident + shared &&
                    fixture.budget.peak() == resident + shared + (std::size_t{7} * 20736),
                "seven native test threads synchronize actual four-plane storage acquisitions; "
                "launch or task failure releases waiters before joining");
    }
    require(fixture.budget.used() == resident &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "joined actual storage-only control refunds every shared/band owner without claiming "
            "method execution");
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working NLM actual map interior retains changed prefix and untouched later rows") {
    const auto assertions_before = assertion_counter().load();
    const auto processing = first_band_processing_visits();
    NlmWorkingFixture healthy{7, 65, nlm_working_test_scratch};
    NlmWorkingFixture stopped{7, 65, nlm_working_test_scratch};
    const auto before = gamma_bits(stopped.luminance.view().storage());
    const auto held = stopped.budget.used();
    const methods::NlmParameters p{.h = 1, .patch = 3, .search = 7};
    const exec::Scheduler normal{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto completed = run_nlm(healthy, p, normal);
    require(completed && completed->counts.changed > 0,
            "healthy actual public NLM mapping attains corrections");
    const CheckpointStop stop{core::Checkpoint::processing, processing + 32};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_nlm(stopped, p, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == processing + 33 && stopped.budget.used() == held,
            "actual map row32 interior stop returns no Facts and refunds shared and band owners");
    const auto output = stopped.luminance.view().storage();
    const auto expected = healthy.luminance.view().storage();
    const auto pitch = stopped.luminance.view().row_pitch();
    bool changed_prefix = false;
    bool later_would_change = false;
    for (std::size_t i = 0; i < output.size(); ++i) {
        if (i / pitch < 32) {
            require(output.subspan(i, 1).front() == expected.subspan(i, 1).front(),
                    "attained prefix equals healthy actual mapping");
            changed_prefix = changed_prefix || std::bit_cast<std::uint64_t>(
                                                   output.subspan(i, 1).front()) != before.at(i);
        } else {
            require(std::bit_cast<std::uint64_t>(output.subspan(i, 1).front()) == before.at(i),
                    "actual later suffix and padding stay untouched after interior stop");
            later_would_change =
                later_would_change ||
                std::bit_cast<std::uint64_t>(expected.subspan(i, 1).front()) != before.at(i);
        }
    }
    require(changed_prefix && later_would_change,
            "private partial Y has real prefix writes and healthy later WOULD-change destinations");
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working NLM larger literal cutoff table is actually charged and refused at its real "
          "acquisition") {
    const auto assertions_before = assertion_counter().load();
    constexpr std::size_t table_bytes = 26059840;
    for (const auto short_table : {false, true}) {
        const auto extra =
            short_table ? std::size_t{1344} + table_bytes - 1 : std::size_t{26074496};
        NlmWorkingFixture fixture{21, 21, extra};
        for (std::uint32_t y = 0; y < 21; ++y) {
            std::ranges::fill(fixture.luminance.view().row(y), 0.5);
        }
        const auto held = fixture.budget.used();
        const auto before = gamma_bits(fixture.luminance.view().storage());
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
        const auto result = run_nlm(fixture, {.h = 25, .patch = 13, .search = 21}, scheduler);
        require(fixture.budget.used() == held &&
                    gamma_bits(fixture.luminance.view().storage()) == before,
                "large actual table success/refusal returns all owners and preserves active "
                "constant Y");
        if (short_table) {
            require(
                !result && result.error().code == core::ErrorCode::resource &&
                    fixture.budget.peak() == held + 1344,
                "one-byte-short exact literal table allocation reaches after real Q then refuses");
        } else {
            require(result && result->weight_table_entries == 3257476 &&
                        result->counts.evaluated == 441 && result->corrected_samples == 0 &&
                        fixture.budget.peak() == held + extra,
                    "actual h25/p13/s21 table has3257476 entries and earns its complete aligned "
                    "phase peak");
        }
    }
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working NLM actual uint32 patch sums attain their maximum beyond uint16") {
    const auto assertions_before = assertion_counter().load();
    NlmWorkingFixture fixture{41, 41, nlm_working_test_scratch};
    for (std::uint32_t y = 0; y < 41; ++y) {
        std::ranges::fill(fixture.luminance.view().row(y), 0);
    }
    for (std::uint32_t y = 13; y <= 27; ++y) {
        std::ranges::fill(fixture.luminance.view().row(y).subspan(13, 15), 1);
    }
    const auto method = methods::Nlm::create({.h = 0.1, .patch = 15, .search = 41}).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const methods::Working working{fixture.luminance.view(), fixture.protection.view().as_const(),
                                   fixture.budget, scheduler};
    const auto held = fixture.budget.used();
    const auto before = gamma_bits(fixture.luminance.view().storage());
    {
        const auto prepared = methods::prepare_nlm(method, working);
        require(prepared.has_value(), "actual maximum-window code and fold owners prepare");
        const methods::NlmContext context{method, working, *prepared};
        constexpr methods::NlmBand band{.first = 0, .rows = 1};
        auto storage = methods::nlm_storage(context, band);
        require(storage.has_value(), "actual uint32 maximum-window storage acquires");
        constexpr methods::NlmOffset offset{.x = 20, .y = 20};
        require(methods::nlm_distances(context, band, offset, *storage).has_value() &&
                    methods::nlm_accumulate(context, band, offset, *storage).has_value() &&
                    storage->columns.view().row(0).front() == 14745375 &&
                    storage->accumulation.view().row(1).front() == 0,
                "225 independently folded opposite extreme codes attain225*65535 exactly and "
                "reject the out-of-table distance");
    }
    require(fixture.budget.used() == held &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "maximum-distance private control refunds every owner without Y stores");
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
