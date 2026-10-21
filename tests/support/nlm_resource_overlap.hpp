// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/nlm_facts.hpp"
#include "nlm_resource_cases.hpp"

#include <array>
#include <atomic>
#include <optional>
#include <thread>

namespace docenhance::tests {
struct NlmStorageGate {
    static inline std::atomic<unsigned> arrivals{0};
    static inline std::atomic<bool> released{false};
    static inline std::atomic<bool> aborted{false};
    static void reset() {
        arrivals.store(0);
        released.store(false);
        aborted.store(false);
    }
    static void release(bool abort) noexcept {
        if (abort) {
            aborted.store(true);
            arrivals.store(2);
        }
        released.store(true);
        released.notify_all();
        arrivals.notify_all();
    }
    static bool observe(core::Checkpoint phase) noexcept {
        if (phase == core::Checkpoint::initialization && !released.load()) {
            arrivals.fetch_add(1);
            arrivals.notify_all();
            while (!released.load()) {
                released.wait(false);
            }
        }
        return aborted.load();
    }
};
inline std::size_t nlm_single_call_peak(methods::NlmParameters parameters) {
    NlmWorkingFixture fixture{7, 7, nlm_working_test_scratch};
    fixture.protection.view().row(0)[0] = 255;
    const auto resident = fixture.budget.used();
    const auto facts = check_nlm_reference(fixture, parameters, 1);
    require(facts.counts.changed > 0, "single-call peak calibration attains actual changed stores");
    return fixture.budget.peak() - resident;
}
inline bool nlm_overlap_facts(const std::array<std::optional<methods::NlmFacts>, 2>& facts,
                              const methods::NlmFacts& expected) {
    const auto& first = facts.front();
    const auto& second = facts.back();
    return first && second && *first == expected && *second == expected;
}
inline void nlm_resource_overlap() {
    NlmWorkingFixture first{7, 7, 0};
    NlmWorkingFixture second{7, 7, 0};
    first.protection.view().row(0)[0] = 255;
    second.protection.view().row(0)[0] = 255;
    const NlmBacking first_before{first};
    const NlmBacking second_before{second};
    const methods::NlmParameters parameters{.h = 1, .patch = 3, .search = 7};
    const auto reference = nlm_working_reference(first, parameters);
    const auto single_peak = nlm_single_call_peak(parameters);
    core::Budget scratch{nlm_working_test_scratch};
    const auto method = methods::Nlm::create(parameters);
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(method && concurrency, "overlap uses an admitted method and single-worker schedulers");
    const exec::Scheduler scheduler{*concurrency, core::Cancellation{{}, &NlmStorageGate::observe}};
    std::array<std::optional<methods::NlmFacts>, 2> facts;
    std::array<std::jthread, 2> threads;
    NlmStorageGate::reset();
    const auto task = [&](NlmWorkingFixture& fixture, std::size_t index) noexcept {
        try {
            const auto result =
                methods::apply(*method, {fixture.luminance.view(),
                                         fixture.protection.view().as_const(), scratch, scheduler});
            if (result) {
                facts.at(index) = *result;
            } else {
                NlmStorageGate::release(true);
            }
        } catch (...) {
            NlmStorageGate::release(true);
        }
    };
    try {
        threads.at(0) = std::jthread{[&] { task(first, 0); }};
        threads.at(1) = std::jthread{[&] { task(second, 1); }};
    } catch (...) {
        NlmStorageGate::release(true);
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        throw;
    }
    while (NlmStorageGate::arrivals.load() < 2 && !NlmStorageGate::aborted.load()) {
        const auto arrived = NlmStorageGate::arrivals.load();
        if (arrived < 2 && !NlmStorageGate::aborted.load()) {
            NlmStorageGate::arrivals.wait(arrived);
        }
    }
    const auto overlapping = scratch.used();
    const bool both_owned = NlmStorageGate::arrivals.load() == 2 &&
                            !NlmStorageGate::aborted.load() && overlapping > single_peak;
    NlmStorageGate::release(false);
    for (auto& thread : threads) {
        thread.join();
    }
    require(both_owned && nlm_overlap_facts(facts, reference.facts) && scratch.used() == 0,
            "two actual calls simultaneously hold shared scratch before independent maps/refunds");
    nlm_resource_complete(first, first_before, reference);
    nlm_resource_complete(second, second_before, reference);
    require(gamma_bits(first.luminance.view().storage()) ==
                gamma_bits(second.luminance.view().storage()),
            "concurrent disjoint mutable frames retain identical complete output bits");
}
} // namespace docenhance::tests
