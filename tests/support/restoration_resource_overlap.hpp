// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "fft_allocation_gate.hpp"
#include "fft_detail.hpp"
#include "restoration_resource_observation.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

namespace docenhance::tests {
namespace restoration_gate = fft_allocation_gate;
struct RestorationOverlap {
    explicit RestorationOverlap(unsigned count)
        : callers(count), method(resource_wiener()), psf(resource_psf()),
          scheduler(resource_concurrency()) {
        for (unsigned i = 0; i < callers; ++i) {
            fixtures.at(i) = std::make_unique<RestorationResourceFixture>(budget, extent);
        }
    }
    static constexpr image::Extent extent{.width = 503, .height = 211};
    unsigned callers;
    core::Budget budget{restoration_resource_limit};
    methods::Wiener method;
    methods::ResolvedPsf psf;
    exec::Scheduler scheduler;
    std::array<std::unique_ptr<RestorationResourceFixture>, restoration_gate::worker_limit>
        fixtures{};
    std::array<std::optional<methods::WienerFacts>, restoration_gate::worker_limit> facts{};
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
};
inline void restoration_overlap_worker(RestorationOverlap& probe, unsigned index) noexcept {
    restoration_warm_exception_runtime();
    probe.ready.fetch_add(1);
    while (!probe.start.load()) {
        std::this_thread::yield();
    }
    if (restoration_gate::failed.load()) {
        return;
    }
    try {
        const auto result = methods::apply(probe.method, probe.psf,
                                           probe.fixtures.at(index)->working(probe.scheduler));
        if (result) {
            probe.facts.at(index) = *result;
        } else {
            restoration_gate::failed.store(true);
        }
    } catch (...) {
        restoration_gate::failed.store(true);
    }
    restoration_gate::finish();
}
inline void restoration_join(std::span<std::jthread> threads) {
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}
struct RestorationOverlapCleanup {
    std::reference_wrapper<RestorationOverlap> probe;
    std::span<std::jthread> threads;
    RestorationOverlapCleanup(RestorationOverlap& owner, std::span<std::jthread> workers)
        : probe(owner), threads(workers) {}
    RestorationOverlapCleanup(const RestorationOverlapCleanup&) = delete;
    RestorationOverlapCleanup& operator=(const RestorationOverlapCleanup&) = delete;
    RestorationOverlapCleanup(RestorationOverlapCleanup&&) = delete;
    RestorationOverlapCleanup& operator=(RestorationOverlapCleanup&&) = delete;
    ~RestorationOverlapCleanup() {
        if (std::ranges::any_of(threads, [](auto& thread) { return thread.joinable(); })) {
            restoration_gate::failed.store(true);
            restoration_gate::disable();
            probe.get().start.store(true);
            restoration_join(threads);
        }
    }
};
struct RestorationOverlapObservation {
    std::size_t simultaneous = 0, charge = 0, peak = 0, live = 0, workers = 0;
    unsigned arrivals = 0;
};
inline RestorationOverlapObservation restoration_overlap_run(RestorationOverlap& probe) {
    std::array<std::jthread, restoration_gate::worker_limit> threads;
    const RestorationOverlapCleanup cleanup{probe, threads};
    restoration_gate::disable();
    restoration_gate::failed.store(false);
    try {
        for (unsigned i = 0; i < probe.callers; ++i) {
            threads.at(i) = std::jthread{[&probe, i] { restoration_overlap_worker(probe, i); }};
        }
    } catch (...) {
        restoration_gate::failed.store(true);
        restoration_gate::disable();
        probe.start.store(true);
        restoration_join(threads);
        throw;
    }
    while (probe.ready.load() != probe.callers) {
        std::this_thread::yield();
    }
    RestorationOverlapObservation observed;
    {
        const RestorationObservationScope scope{0};
        restoration_gate::arm((std::size_t{576} * fft::native_lanes() * sizeof(double)) +
                              core::buffer_alignment);
        probe.start.store(true);
        while (restoration_gate::arrivals.load() != probe.callers &&
               !restoration_gate::failed.load()) {
            std::this_thread::yield();
        }
        observed.simultaneous = restoration_allocation::live.load();
        observed.charge = probe.budget.used();
        observed.arrivals = restoration_gate::arrivals.load();
        restoration_gate::released.store(true);
        restoration_join(threads);
        restoration_gate::disable();
        observed.peak = restoration_allocation::peak.load();
        observed.live = restoration_allocation::live.load();
        observed.workers = restoration_allocation::worker_count;
    }
    return observed;
}
inline void restoration_overlap_case(unsigned callers) {
    const auto bound = resource_bound(RestorationOverlap::extent);
    const auto visibility = restoration_payload_visibility(bound);
    const restoration_trace::SpillSession trace_session{callers};
    RestorationOverlap probe{callers};
    const auto coefficients = probe.psf.coefficients;
    const auto baseline = probe.budget.used();
    const auto phase = fft::phase_bytes(bound.transform, 1, fft::Axis::rows);
    require(phase.has_value(), "whole-method overlap has actual first-row provider capacity");
    const auto observed = restoration_overlap_run(probe);
    require(!restoration_gate::failed.load() && observed.arrivals == callers &&
                observed.simultaneous > 0 && observed.live == 0 && observed.workers == callers &&
                probe.budget.used() == baseline && restoration_allocation::observation_valid(),
            "all actual whole-method callers overlap native storage then release and join");
    const auto owned = bound.real + bound.spectrum;
    require(
        observed.charge == baseline + (callers * (owned + *phase)) &&
            observed.simultaneous <= callers * (*phase + visibility.real + visibility.spectrum) &&
            observed.peak <= callers * (bound.native + visibility.observed_payload()) &&
            probe.budget.peak() <= baseline + (callers * bound.peak),
        "simultaneous native heap and exact charged payload/row leases obey separate capacities");
    for (unsigned i = 0; i < callers; ++i) {
        const auto& facts = probe.facts.at(i);
        const auto& first = probe.facts.front();
        require(facts.has_value(), "each actual simultaneous complete method succeeds");
        if (!facts.has_value() || !first.has_value()) {
            throw std::logic_error("Required simultaneous restoration Facts are absent");
        }
        resource_facts(*facts, RestorationOverlap::extent);
        probe.fixtures.at(i)->preserved(false);
        require(*facts == *first &&
                    gamma_bits(probe.fixtures.at(i)->luminance.view().storage()) ==
                        gamma_bits(probe.fixtures.front()->luminance.view().storage()),
                "identical entering frames independently retain identical full output and Facts");
    }
    require(probe.psf.coefficients == coefficients, "shared resolved PSF remains immutable");
    std::cout << "{\"whole_method_callers\":" << callers << ",\"arrivals\":" << observed.arrivals
              << ",\"simultaneous_provider_live\":" << observed.simultaneous
              << ",\"simultaneous_charge\":" << observed.charge << ",\"resident\":" << baseline
              << ",\"heap_peak\":" << observed.peak << ",\"charged_peak\":" << probe.budget.peak()
              << ",\"events\":";
    restoration_trace::write(std::cout);
    std::cout << "}\n";
}
} // namespace docenhance::tests
