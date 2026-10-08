// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "allocation_observer.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/opencv/restoration.hpp"
#include "linear_fixture.hpp"
#include "observed_mat_allocator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <new>
#include <opencv2/core/mat.hpp>
#include <utility>
namespace {
namespace allocation = docenhance::tests::allocation_observer;
namespace core = docenhance::core;
namespace image = docenhance::image;
namespace methods = docenhance::methods;
namespace native = docenhance::opencv;
struct Observation {
    bool success = false;
    bool resource = false;
    std::size_t peak = 0;
    std::size_t attempts = 0;
    std::uint64_t external = 0;
    std::uint64_t charge = 0;
    core::ErrorCode code = core::ErrorCode::invariant;
    std::size_t live = 0;
    std::size_t retained_charge = 0;
    std::uint32_t native_calls = 0;
};
class ObservationScope {
  public:
    explicit ObservationScope(cv::MatAllocator& allocator)
        : original_(cv::Mat::getDefaultAllocator()) {
        cv::Mat::setDefaultAllocator(&allocator);
        allocation::observing.store(true);
    }
    ~ObservationScope() {
        allocation::observing.store(false);
        cv::Mat::setDefaultAllocator(original_);
    }
    ObservationScope(const ObservationScope&) = delete;
    ObservationScope& operator=(const ObservationScope&) = delete;
    ObservationScope(ObservationScope&&) = delete;
    ObservationScope& operator=(ObservationScope&&) = delete;

  private:
    cv::MatAllocator* original_;
};
double sample(std::uint32_t x, std::uint32_t y) {
    constexpr std::uint32_t period = 997;
    constexpr double scale = 1994;
    return 0.2 + (static_cast<double>((x + (17 * y)) % period) / scale);
}
void fill(docenhance::tests::LinearFixture& source) {
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        const auto row = source.row(y);
        for (std::uint32_t x = 0; x < source.extent().width; ++x) {
            std::ranges::fill(
                row.subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels),
                sample(x, y));
        }
    }
}
bool preserved(docenhance::tests::LinearFixture& source) {
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        const auto row = source.row(y);
        for (std::size_t i = 0; i < row.size(); ++i) {
            const auto x = static_cast<std::uint32_t>(i / image::rgb_channels);
            if (row[i] != sample(x, y)) {
                return false;
            }
        }
    }
    return true;
}
Observation observe(docenhance::tests::LinearFixture& source, const methods::Wiener& method,
                    core::Budget& budget, std::size_t failure) {
    auto psf = native::resolve_psf(method).value();
    const auto guard = std::max(32U, 4 * std::max(psf.width / 2, psf.height / 2));
    const image::Extent fft{
        .width = methods::restoration_fft_size(source.extent().width + (2 * guard)).value(),
        .height = methods::restoration_fft_size(source.extent().height + (2 * guard)).value(),
    };
    const auto charge = methods::restoration_charge(source.extent(), fft).value();
    const auto baseline = budget.used();
    allocation::peak.store(0);
    allocation::allocation_attempts.store(0);
    allocation::failure_at.store(failure);
    methods::RestorationReport report;
    Observation result{.charge = charge.peak};
    docenhance::tests::ObservedMatAllocator allocator{*cv::Mat::getDefaultAllocator()};
    {
        const ObservationScope scope{allocator};
        auto model = native::RestorationModel::prepare(source, {}, method, std::move(psf),
                                                       {budget, {}, report, image::RowUse::output});
        result.success = model.has_value();
        result.resource = !model && model.error().code == core::ErrorCode::resource;
        if (!model) {
            result.code = model.error().code;
        }
        if (report.preparation_charge_peak > baseline + report.native_reserved_bytes) {
            result.external =
                report.preparation_charge_peak - baseline - report.native_reserved_bytes;
        }
    }
    allocation::failure_at.store(0);
    result.peak = allocation::peak.load();
    result.attempts = allocation::allocation_attempts.load();
    result.live = allocation::live.load();
    result.retained_charge = budget.used() - baseline;
    result.native_calls = report.native_calls;
    if (budget.used() != baseline || allocation::live.load() != 0 || !preserved(source)) {
        result.success = false;
        result.resource = false;
    }
    return result;
}
bool measure(image::Extent extent) {
    constexpr std::size_t limit = std::size_t{128} * 1024 * 1024;
    core::Budget budget{limit};
    docenhance::tests::LinearFixture source{budget, extent};
    fill(source);
    const auto method =
        methods::Wiener::create({.psf = methods::GaussianPsf{.sigma = 1.2}}).value();
    {
        methods::RestorationReport warm_report;
        auto psf = native::resolve_psf(method).value();
        const auto warm = native::RestorationModel::prepare(
            source, {}, method, std::move(psf), {budget, {}, warm_report, image::RowUse::output});
        if (!warm) {
            return false;
        }
    }
    const auto observation = observe(source, method, budget, 0);
    // Ordinary hooks observe FFT new/AutoBuffer and native Mat payloads; charged aligned
    // external planes are measured independently by the live budget. Sanitizer hooks observe
    // all malloc storage, so adding external storage there would double-count the same bytes.
    const auto measured =
        observation.peak + (DE_ALLOCATION_SANITIZER_OBSERVATION ? 0 : observation.external);
    if (!observation.success || observation.attempts == 0 || measured > observation.charge) {
        return false;
    }
    for (std::size_t fail = 1; fail <= observation.attempts; ++fail) {
        const auto refused = observe(source, method, budget, fail);
        if (!refused.resource) {
            std::cerr << "Restoration allocation failure " << fail << "/" << observation.attempts
                      << " not contained; attempts=" << refused.attempts
                      << " code=" << static_cast<int>(refused.code) << " live=" << refused.live
                      << " charge=" << refused.retained_charge
                      << " nativecalls=" << refused.native_calls << "\n";
            return false;
        }
    }
    std::cout << "{\"width\":" << extent.width << ",\"height\":" << extent.height
              << ",\"observed_heap_peak\":" << observation.peak
              << ",\"charged_external\":" << observation.external
              << ",\"conservative_observed_peak\":" << measured
              << ",\"reserved_peak\":" << observation.charge
              << ",\"contained_allocation_failures\":" << observation.attempts << "}\n";
    return true;
}
void warm_exception_runtime() {
    // Initialize this thread's exception runtime before measuring injected failures.
    // Darwin's first __cxa_throw allocates persistent exception TLS through calloc;
    // LLDB identified that 16-byte block at __cxa_get_globals, not a native FFT leak.
    try {
        throw std::bad_alloc{};
    } catch (const std::bad_alloc&) {
        return;
    }
}
} // namespace
int main() {
    try {
        if (!allocation::initialize_hooks()) {
            return 1;
        }
        warm_exception_runtime();
        constexpr auto cases = std::to_array<image::Extent>({
            {.width = 1, .height = 1},
            {.width = 1, .height = 409},
            {.width = 419, .height = 1},
            {.width = 129, .height = 97},
            {.width = 503, .height = 211},
        });
        return std::ranges::all_of(cases, measure) ? 0 : 1;
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs(error.what(), stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("Restoration observer failure", stderr));
        return 1;
    }
}
