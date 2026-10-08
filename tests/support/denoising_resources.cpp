// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "allocation_observer.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/opencv/nlm.hpp"
#include "observed_mat_allocator.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/core/utils/logger.defines.hpp>
#include <opencv2/core/utils/logger.hpp>
namespace {
namespace allocation = docenhance::tests::allocation_observer;
} // namespace
namespace {
namespace core = docenhance::core;
namespace image = docenhance::image;
namespace methods = docenhance::methods;
namespace denoise = docenhance::opencv;
struct NativeObservation {
    bool completed;
    double seconds;
    std::size_t peak;
    std::size_t allocations;
};
NativeObservation observe_call(docenhance::image::PlaneView<const std::uint16_t> input,
                               docenhance::image::PlaneView<std::uint16_t> output,
                               const docenhance::methods::Nlm& method,
                               docenhance::core::Budget& budget) {
    allocation::observing.store(true);
    const auto started = std::chrono::steady_clock::now();
    const auto applied = docenhance::opencv::native_tile(input, output, method, budget);
    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    allocation::observing.store(false);
    return {
        .completed = applied.has_value(),
        .seconds = seconds,
        .peak = allocation::peak.load(),
        .allocations = allocation::allocation_attempts.load(),
    };
}
int measure() {
    constexpr int requested_threads = 4;
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
    cv::setNumThreads(requested_threads);
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    const auto side = methods::nlm_native_extent;
    auto input = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    auto output = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    constexpr std::uint32_t multiplier = 167;
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            input.view().row(y).subspan(x, 1).front() = static_cast<std::uint16_t>(
                ((x * multiplier) + y) % (std::uint32_t{UINT16_MAX} + 1));
        }
    }
    const auto method =
        methods::Nlm::create({.patch = methods::nlm_max_patch, .search = methods::nlm_max_search})
            .value();
    if (!denoise::native_tile(input.view().as_const(), output.view(), method, budget)) {
        return 1;
    }
    auto* const original = cv::Mat::getDefaultAllocator();
    docenhance::tests::ObservedMatAllocator allocator{*original};
    {
        docenhance::tests::ObservedMatAllocator refused_allocator{*original, true};
        cv::Mat::setDefaultAllocator(&refused_allocator);
        const auto refused =
            denoise::native_tile(input.view().as_const(), output.view(), method, budget);
        cv::Mat::setDefaultAllocator(original);
        if (refused || refused.error().code != core::ErrorCode::resource) {
            return 1;
        }
    }
    cv::Mat::setDefaultAllocator(&allocator);
    const auto observation = observe_call(input.view().as_const(), output.view(), method, budget);
    cv::Mat::setDefaultAllocator(original);
    const auto bound = methods::nlm_native_scratch({.width = side, .height = side}, method).value();
    const bool fits = observation.completed && allocation::live.load() == 0 &&
                      observation.peak <= bound && allocation::worker_count == 1;
    std::size_t contained = 0;
    for (std::size_t fail = 1; fail <= observation.allocations; ++fail) {
        allocation::allocation_attempts.store(0);
        allocation::failure_at.store(fail);
        cv::Mat::setDefaultAllocator(&allocator);
        allocation::observing.store(true);
        bool refused = false;
        {
            const auto result =
                denoise::native_tile(input.view().as_const(), output.view(), method, budget);
            refused = !result && result.error().code == core::ErrorCode::resource;
        }
        allocation::observing.store(false);
        cv::Mat::setDefaultAllocator(original);
        if (!refused || allocation::live.load() != 0) {
            std::cerr << "Native allocation failure " << fail << "/" << observation.allocations
                      << ": resource=" << refused << "; live=" << allocation::live.load()
                      << "; attempts=" << allocation::allocation_attempts.load() << "\n";
            return 1;
        }
        ++contained;
    }
    std::cout << "{\"observed_native_peak\":" << observation.peak
              << ",\"reserved_native_bound\":" << bound
              << ",\"native_live_after\":" << allocation::live.load()
              << ",\"native_tile_seconds\":" << observation.seconds
              << ",\"allocation_workers\":" << allocation::worker_count
              << ",\"requested_native_threads\":" << requested_threads
              << ",\"contained_allocation_failures\":" << contained << "}\n";
    return fits ? 0 : 1;
}
} // namespace
int main() {
    try {
        if (!allocation::initialize_hooks()) {
            return 1;
        }
        return measure();
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs("Native allocation observer exception: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("Native allocation observer nonstandard exception\n", stderr));
        return 1;
    }
}
