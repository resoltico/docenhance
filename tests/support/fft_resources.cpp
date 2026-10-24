// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "allocation_calibration.hpp"
#include "allocation_observer.hpp"
#include "color_context_cases.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/plane.hpp"
#include "fft_allocation_gate.hpp"
#include "fft_allocation_trace.hpp"
#include "fft_detail.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <thread>
#include <vector>
namespace {
namespace allocation = docenhance::tests::allocation_observer;
namespace trace = docenhance::tests::fft_allocation_trace;
namespace gate = docenhance::tests::fft_allocation_gate;
using docenhance::tests::allocation_calibration::cpp_self_check;
using docenhance::tests::allocation_calibration::reset_observation;
using docenhance::tests::allocation_calibration::self_check;
using docenhance::tests::allocation_calibration::warm_exception_runtime;
namespace core = docenhance::core;
namespace exec = docenhance::exec;
namespace fft = docenhance::fft;
namespace image = docenhance::image;
constexpr std::size_t resource_budget = std::size_t{64} * 1024 * 1024;
struct NativeFixture {
    NativeFixture(std::uint32_t width, std::uint32_t height)
        : real(image::Plane<double>::allocate(budget, width, height).value()),
          spectrum(make_spectrum()) {}
    fft::Spectrum make_spectrum() {
        std::ranges::fill(real.view().storage(), 0.25);
        return fft::forward(real.view().as_const(), budget, scheduler).value();
    }
    core::Budget budget{resource_budget};
    exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    image::Plane<double> real;
    fft::Spectrum spectrum;
};
struct NativeObservation {
    bool success = false;
    core::ErrorCode code = core::ErrorCode::invariant;
    std::size_t peak = 0;
    std::size_t attempts = 0;
    std::size_t live = 0;
    std::size_t c_attempts = 0;
};
NativeObservation observe(NativeFixture& fixture, fft::Axis axis, bool inverse, std::uint32_t count,
                          std::size_t fail) {
    reset_observation();
    allocation::failure_at.store(fail);
    const fft::Transform transform{fixture.real.view().as_const(), fixture.real.view(),
                                   fixture.spectrum, fixture.scheduler, inverse};
    bool success = false;
    auto code = core::ErrorCode::invariant;
    allocation::observing.store(true);
    {
        const auto result = axis == fft::Axis::rows ? fft::native_rows(transform, 0, count)
                                                    : fft::native_columns(transform, 0, count);
        success = result.has_value();
        code = result ? core::ErrorCode::invariant : result.error().code;
    }
    allocation::observing.store(false);
    allocation::failure_at.store(0);
    std::cout << "{\"fail\":" << fail << ",\"count\":" << count
              << ",\"rows\":" << (axis == fft::Axis::rows) << ",\"inverse\":" << inverse
              << ",\"width\":" << fixture.spectrum.extent().width
              << ",\"height\":" << fixture.spectrum.extent().height << ",\"events\":";
    trace::write(std::cout);
    std::cout << "}\n";
    return {
        .success = success,
        .code = code,
        .peak = allocation::peak.load(),
        .attempts = allocation::allocation_attempts.load(),
        .live = allocation::live.load(),
        .c_attempts = allocation::c_allocation_attempts.load(),
    };
}
bool measure(std::uint32_t length, std::uint32_t count, fft::Axis axis, bool inverse) {
    const auto rows = axis == fft::Axis::rows;
    NativeFixture fixture{rows ? length : 128, rows ? 64 : length};
    const auto bound = fft::phase_bytes(fixture.spectrum.extent(), 1, axis).value();
    const auto held = fixture.budget.used();
    const auto healthy = observe(fixture, axis, inverse, count, 0);
    if (!healthy.success || healthy.attempts == 0 || healthy.live != 0 || healthy.peak > bound ||
        (length > 1 && healthy.c_attempts == 0) || fixture.budget.used() != held) {
        std::cerr << "Native FFT healthy bound/refund failed: length=" << length
                  << " count=" << count << " rows=" << rows << " inverse=" << inverse
                  << " peak=" << healthy.peak << " bound=" << bound
                  << " attempts=" << healthy.attempts << " live=" << healthy.live << "\n";
        return false;
    }
    for (std::size_t fail = 1; fail <= healthy.attempts; ++fail) {
        const auto refused = observe(fixture, axis, inverse, count, fail);
        if (refused.success || refused.code != core::ErrorCode::resource ||
            refused.attempts < fail || refused.live != 0 || fixture.budget.used() != held) {
            std::cerr << "Native FFT failure not contained/reached: length=" << length
                      << " count=" << count << " rows=" << rows << " inverse=" << inverse
                      << " fail=" << fail << " attempts=" << refused.attempts
                      << " code=" << static_cast<int>(refused.code) << " live=" << refused.live
                      << "\n";
            return false;
        }
    }
    std::cout << "{\"length\":" << length << ",\"count\":" << count << ",\"rows\":" << rows
              << ",\"inverse\":" << inverse << ",\"observed_heap_peak\":" << healthy.peak
              << ",\"reserved_phase\":" << bound << ",\"c_attempts\":" << healthy.c_attempts
              << ",\"reached_failures\":" << healthy.attempts << "}\n";
    return true;
}
bool native_cases() {
    for (const auto length : {1U, 2U, 3U, 4U, 5U, 8U, 15U, 60U, 125U, 256U, 864U, 1000U}) {
        for (const auto count : {1U, 2U, 3U, 63U, 64U}) {
            if (!measure(length, count, fft::Axis::rows, false) ||
                !measure(length, count, fft::Axis::rows, true) ||
                !measure(length, count, fft::Axis::columns, false) ||
                !measure(length, count, fft::Axis::columns, true)) {
                return false;
            }
        }
    }
    return true;
}
struct ConcurrentProbe {
    ConcurrentProbe(unsigned count, fft::Axis direction, bool backward)
        : workers(count), axis(direction), inverse(backward) {
        for (unsigned i = 0; i < workers; ++i) {
            fixtures.at(i) = std::make_unique<NativeFixture>(864, 480);
        }
    }
    unsigned workers;
    fft::Axis axis;
    bool inverse;
    std::array<std::unique_ptr<NativeFixture>, gate::worker_limit> fixtures{};
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
};
void concurrent_worker(ConcurrentProbe& probe, unsigned index) noexcept {
    warm_exception_runtime();
    probe.ready.fetch_add(1);
    while (!probe.start.load()) {
        std::this_thread::yield();
    }
    if (gate::failed.load()) {
        return;
    }
    try {
        auto& fixture = *probe.fixtures.at(index);
        const fft::Transform transform{fixture.real.view().as_const(), fixture.real.view(),
                                       fixture.spectrum, fixture.scheduler, probe.inverse};
        const auto total = probe.axis == fft::Axis::rows ? fixture.spectrum.extent().height
                                                         : fixture.spectrum.shape().width;
        const auto first = index * fft::block_size;
        const auto count = std::min(fft::block_size, total - first);
        const auto result = probe.axis == fft::Axis::rows
                                ? fft::native_rows(transform, first, count)
                                : fft::native_columns(transform, first, count);
        if (!result) {
            gate::failed.store(true);
        }
    } catch (...) {
        gate::failed.store(true);
    }
    gate::finish();
}
void join_workers(std::span<std::jthread> threads) {
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}
bool concurrent_case(unsigned workers, fft::Axis axis, bool inverse) {
    ConcurrentProbe probe{workers, axis, inverse};
    std::array<std::jthread, gate::worker_limit> threads;
    gate::disable();
    gate::failed.store(false);
    try {
        for (unsigned i = 0; i < workers; ++i) {
            threads.at(i) = std::jthread([&probe, i] { concurrent_worker(probe, i); });
        }
    } catch (...) {
        gate::failed.store(true);
        probe.start.store(true);
        join_workers(threads);
        return false;
    }
    while (probe.ready.load() != workers) {
        std::this_thread::yield();
    }
    allocation::reset_workers();
    allocation::allocation_attempts.store(0);
    allocation::c_allocation_attempts.store(0);
    allocation::peak.store(0);
    trace::reset();
    const auto rows = axis == fft::Axis::rows;
    const auto length = rows ? 864U : 480U;
    const auto element = rows ? sizeof(double) : sizeof(fft::Complex);
    const auto padding = fft::native_lanes() == 1 ? 0U : core::buffer_alignment;
    gate::arm((std::size_t{length} * fft::native_lanes() * element) + padding);
    allocation::observing.store(true);
    probe.start.store(true);
    while (gate::arrivals.load() != workers && !gate::failed.load()) {
        std::this_thread::yield();
    }
    const auto simultaneous = allocation::live.load();
    gate::released.store(true);
    join_workers(threads);
    allocation::observing.store(false);
    gate::disable();
    const auto bound =
        fft::phase_bytes(probe.fixtures.front()->spectrum.extent(), workers, axis).value();
    std::cout << "{\"workers\":" << workers << ",\"rows\":" << rows << ",\"inverse\":" << inverse
              << ",\"arrivals\":" << gate::arrivals.load()
              << ",\"simultaneous_live\":" << simultaneous
              << ",\"peak\":" << allocation::peak.load() << ",\"bound\":" << bound
              << ",\"events\":";
    trace::write(std::cout);
    std::cout << "}\n";
    return !gate::failed.load() && gate::arrivals.load() == workers && simultaneous != 0 &&
           allocation::worker_count == workers && allocation::live.load() == 0 &&
           allocation::peak.load() <= bound;
}
bool concurrent_cases() {
    for (const auto workers : {1U, 2U, 7U}) {
        for (const auto axis : {fft::Axis::rows, fft::Axis::columns}) {
            if (!concurrent_case(workers, axis, false) || !concurrent_case(workers, axis, true)) {
                return false;
            }
        }
    }
    return true;
}
std::atomic<unsigned> late_stop_visits{0};
std::atomic<unsigned> native_entries{0};
bool stop_after_failure(core::Checkpoint phase) noexcept {
    if (phase == core::Checkpoint::solving) {
        native_entries.fetch_add(1);
    }
    if (allocation::allocation_attempts.load() >= 3) {
        late_stop_visits.fetch_add(1);
        return true;
    }
    return false;
}
bool native_error_precedence(bool inverse) {
    NativeFixture fixture{8, 8};
    const auto source = std::as_bytes(fixture.spectrum.storage());
    const std::vector<std::byte> before(source.begin(), source.end());
    const auto real = std::as_bytes(fixture.real.view().storage());
    const std::vector<std::byte> real_before(real.begin(), real.end());
    const auto held = fixture.budget.used();
    const core::Cancellation cancellation{{}, stop_after_failure};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(), cancellation};
    late_stop_visits.store(0);
    native_entries.store(0);
    reset_observation();
    allocation::failure_at.store(3);
    allocation::observing.store(true);
    bool resource = false;
    if (inverse) {
        const auto result =
            fft::inverse(fixture.spectrum, fixture.real.view(), fixture.budget, scheduler);
        resource = !result && result.error().code == core::ErrorCode::resource;
    } else {
        const auto result = fft::forward(fixture.real.view().as_const(), fixture.budget, scheduler);
        resource = !result && result.error().code == core::ErrorCode::resource;
    }
    allocation::observing.store(false);
    allocation::failure_at.store(0);
    // Error transport can allocate after the one-shot refusal at attempt three.
    const bool clean = resource && allocation::allocation_attempts.load() >= 3 &&
                       native_entries.load() == 1 && late_stop_visits.load() == 0 &&
                       allocation::live.load() == 0 && fixture.budget.used() == held &&
                       std::ranges::equal(source, before) &&
                       std::ranges::equal(real, real_before) &&
                       cancellation.requested(core::Checkpoint::admission);
    std::cout << R"({"native_error_precedence":true,"inverse":)" << inverse
              << ",\"passed\":" << clean
              << ",\"attempts\":" << allocation::allocation_attempts.load()
              << ",\"native_entries\":" << native_entries.load()
              << ",\"late_stop_visits\":" << late_stop_visits.load() << ",\"events\":";
    trace::write(std::cout);
    std::cout << "}\n";
    return clean;
}
} // namespace
int main() {
    try {
        warm_exception_runtime();
        if (!allocation::initialize_hooks()) {
            return 1;
        }
        const bool observed_c = self_check();
        const bool observed_cpp = cpp_self_check();
        const bool unobserved_c = self_check(false);
        const bool unobserved_cpp = cpp_self_check(false);
        if (!observed_c || !observed_cpp || !unobserved_c || !unobserved_cpp ||
            !allocation::c_contract_cases()) {
            std::cerr << "Actual C allocator registration/forwarding self-check failed\n";
            return 1;
        }
        std::cout << "Actual observed/unobserved C and owned C++ registration/forwarding "
                     "self-checks passed\n";
        if (!trace::check_storage_contract()) {
            std::cerr << "Synthetic trace storage/order controls failed\n";
            return 1;
        }
        std::cout << "Synthetic bounded trace storage/order controls passed; not native allocation "
                     "evidence\n";
        return docenhance::tests::color_context_cases() && native_cases() && concurrent_cases() &&
                       native_error_precedence(false) && native_error_precedence(true) &&
                       allocation::observation_valid()
                   ? 0
                   : 1;
    } catch (const std::exception& error) {
        allocation::observing.store(false);
        static_cast<void>(std::fputs("FFT resource probe exception: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
        return 1;
    } catch (...) {
        allocation::observing.store(false);
        static_cast<void>(std::fputs("FFT resource probe unknown exception\n", stderr));
        return 1;
    }
}
