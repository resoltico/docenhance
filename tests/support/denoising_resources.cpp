// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/denoise/nlm.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#if !DE_NLM_SANITIZER_OBSERVATION
#include <new>
#endif
#ifdef __has_feature
#if __has_feature(address_sanitizer)
#include <dlfcn.h>
#endif
#endif
#if DE_NLM_SANITIZER_OBSERVATION
#include <sanitizer/allocator_interface.h>
#endif
#include <opencv2/core/exception.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/core/utils/logger.defines.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <thread>

namespace {
std::atomic<bool> observing{false};
std::atomic<std::size_t> live{0};
std::atomic<std::size_t> peak{0};
constexpr std::size_t observer_worker_limit = 8;
std::array<std::thread::id, observer_worker_limit> workers{};
std::mutex worker_mutex;
std::size_t worker_count = 0;
std::atomic<std::size_t> allocation_attempts{0};
std::atomic<std::size_t> failure_at{0};
bool refuse_allocation() noexcept {
    if (!observing.load()) {
        return false;
    }
    const auto attempt = allocation_attempts.fetch_add(1) + 1;
    return failure_at.load() != 0 && attempt == failure_at.load();
}
void acquire_bytes(std::size_t bytes) {
    const std::scoped_lock lock{worker_mutex};
    const auto worker = std::this_thread::get_id();
    const bool known = std::ranges::find(workers, worker) != workers.end();
    if (!known) {
        if (worker_count < workers.size()) {
            workers.at(worker_count) = worker;
            ++worker_count;
        } else {
            worker_count = observer_worker_limit + 1;
        }
    }
    const auto current = live.fetch_add(bytes) + bytes;
    auto maximum = peak.load();
    while (current > maximum && !peak.compare_exchange_weak(maximum, current)) {
    }
}
struct ObservedAllocation {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    bool measured = false;
};
constexpr std::size_t allocation_slots = 2048;
std::array<ObservedAllocation, allocation_slots> allocations{};
std::mutex allocation_mutex;
bool register_allocation(void* const pointer, std::size_t bytes) {
    bool registered = false;
    const bool measured = observing.load();
    {
        const std::scoped_lock lock{allocation_mutex};
        for (auto& slot : allocations) {
            if (slot.pointer == nullptr) {
                slot = {.pointer = pointer, .bytes = bytes, .measured = measured};
                registered = true;
                break;
            }
        }
    }
    if (registered && measured) {
        acquire_bytes(bytes);
    }
    return registered;
}
ObservedAllocation forget_allocation(const void* const pointer) noexcept {
    ObservedAllocation allocation;
    {
        const std::scoped_lock lock{allocation_mutex};
        for (auto& slot : allocations) {
            if (slot.pointer == pointer) {
                allocation = slot;
                slot = {};
                break;
            }
        }
    }
    if (allocation.measured) {
        live.fetch_sub(allocation.bytes);
    }
    return allocation;
}
#if !DE_NLM_SANITIZER_OBSERVATION
void* allocate_bytes(std::size_t bytes) {
    if (refuse_allocation()) {
        throw std::bad_alloc{};
    }
    // The replaceable ABI returns raw storage, released by the paired deallocation callback.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    auto* const pointer = std::malloc(bytes == 0 ? 1 : bytes);
    if (pointer == nullptr) {
        throw std::bad_alloc{};
    }
    const bool registered = register_allocation(pointer, bytes);
    if (!registered) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
        std::free(pointer);
        throw std::bad_alloc{};
    }
    return pointer;
}
void release_external(void* const pointer) noexcept {
#ifdef __has_feature
#if __has_feature(address_sanitizer)
    // Mach-O's sanitizer and standard-library interposition can allocate without this observer.
    // POSIX dlsym provides the next runtime's ordinary delete entry, preserving its allocation tag.
    using DeleteFunction = void (*)(void*);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    static const auto release = reinterpret_cast<DeleteFunction>(dlsym(RTLD_NEXT, "_ZdlPv"));
    if (release == nullptr) {
        std::abort();
    }
    release(pointer);
    return;
#endif
#endif
    // Unobserved ordinary CRT allocations retain the CRT's original malloc/free storage layout.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    std::free(pointer);
}
void release_bytes(void* const pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    const auto allocation = forget_allocation(pointer);
    if (allocation.pointer == nullptr) {
        release_external(pointer);
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    std::free(pointer);
}
#endif
class ObservedMatAllocator final : public cv::MatAllocator {
  public:
    explicit ObservedMatAllocator(cv::MatAllocator& upstream) : upstream_(upstream) {}
    // Fixed public native allocator ABI, including shape, storage and access flags.
    // NOLINTNEXTLINE(readability-function-size)
    cv::UMatData* allocate(int dimensions, const int* sizes, int type, void* data,
                           std::size_t* step, cv::AccessFlag flags,
                           cv::UMatUsageFlags usage) const override {
        if (data == nullptr && refuse_allocation()) {
            CV_Error(cv::Error::StsNoMem, "Observed native Mat allocation refusal");
        }
        auto* const result =
            upstream_.get().allocate(dimensions, sizes, type, data, step, flags, usage);
        if (!DE_NLM_SANITIZER_OBSERVATION && result != nullptr && data == nullptr &&
            observing.load()) {
            acquire_bytes(result->size);
            result->currAllocator = this;
        }
        return result;
    }
    bool allocate(cv::UMatData* data, cv::AccessFlag flags,
                  cv::UMatUsageFlags usage) const override {
        return upstream_.get().allocate(data, flags, usage);
    }
    void deallocate(cv::UMatData* data) const override {
        if (!DE_NLM_SANITIZER_OBSERVATION && data != nullptr) {
            live.fetch_sub(data->size);
            data->currAllocator = &upstream_.get();
        }
        upstream_.get().deallocate(data);
    }

  private:
    std::reference_wrapper<cv::MatAllocator> upstream_;
};
} // namespace
#if DE_NLM_SANITIZER_OBSERVATION
namespace {
thread_local bool hook_active = false;
void observe_malloc(const volatile void* const pointer, std::size_t bytes) noexcept {
    if (!observing.load() || hook_active) {
        return;
    }
    hook_active = true;
    // The sanitizer's observation ABI is const even though allocation storage is owned/mutable.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    const bool recorded = register_allocation(const_cast<void*>(pointer), bytes);
    if (!recorded) {
        std::abort();
    }
    hook_active = false;
}
void observe_free(const volatile void* const pointer) noexcept {
    if (hook_active || (!observing.load() && live.load() == 0)) {
        return;
    }
    hook_active = true;
    // The sanitizer exposes the pointer with volatile observation qualification.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    static_cast<void>(forget_allocation(const_cast<const void*>(pointer)));
    hook_active = false;
}
} // namespace
#else
// These are the standard replaceable ABI signatures, used only in this observation executable.
// Compiler-specific standard-library declarations choose different parameter names.
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void* operator new(std::size_t bytes) {
    return allocate_bytes(bytes);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void* operator new[](std::size_t bytes) {
    return allocate_bytes(bytes);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete(void* const pointer) noexcept {
    release_bytes(pointer);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete[](void* const pointer) noexcept {
    release_bytes(pointer);
}
// Pair nothrow forms too: sanitizer runtimes may otherwise bypass ordinary operator new.
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void* operator new(std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept {
    try {
        return allocate_bytes(bytes);
    } catch (...) {
        return nullptr;
    }
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void* operator new[](std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept {
    try {
        return allocate_bytes(bytes);
    } catch (...) {
        return nullptr;
    }
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete(void* const pointer, const std::nothrow_t& /*tag*/) noexcept {
    release_bytes(pointer);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete[](void* const pointer, const std::nothrow_t& /*tag*/) noexcept {
    release_bytes(pointer);
}
// Complete the replaceable deallocation ABI on compilers emitting sized delete calls.
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete(void* const pointer, std::size_t /*size*/) noexcept {
    release_bytes(pointer);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete[](void* const pointer, std::size_t /*size*/) noexcept {
    release_bytes(pointer);
}
#endif
namespace {
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
    observing.store(true);
    const auto started = std::chrono::steady_clock::now();
    const auto applied = docenhance::denoise::native_tile(input, output, method, budget);
    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    observing.store(false);
    return {
        .completed = applied.has_value(),
        .seconds = seconds,
        .peak = peak.load(),
        .allocations = allocation_attempts.load(),
    };
}
void warm_error_path() {
    try {
        CV_Error(cv::Error::StsNoMem, "Observed native Mat allocation refusal");
    } catch (const cv::Exception& error) {
        if (error.code != cv::Error::StsNoMem) {
            std::abort();
        }
    }
}
int measure() {
#if DE_NLM_SANITIZER_OBSERVATION
    if (__sanitizer_install_malloc_and_free_hooks(observe_malloc, observe_free) == 0) {
        return 1;
    }
#endif
    constexpr int requested_threads = 4;
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
    cv::setNumThreads(requested_threads);
    namespace core = docenhance::core;
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    namespace denoise = docenhance::denoise;
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
    warm_error_path();
    auto* const original = cv::Mat::getDefaultAllocator();
    ObservedMatAllocator allocator{*original};
    cv::Mat::setDefaultAllocator(&allocator);
    const auto observation = observe_call(input.view().as_const(), output.view(), method, budget);
    cv::Mat::setDefaultAllocator(original);
    const auto bound =
        denoise::native_scratch_bytes({.width = side, .height = side}, method).value();
    const bool fits =
        observation.completed && live.load() == 0 && observation.peak <= bound && worker_count == 1;
    std::size_t contained = 0;
    for (std::size_t fail = 1; fail <= observation.allocations; ++fail) {
        allocation_attempts.store(0);
        failure_at.store(fail);
        cv::Mat::setDefaultAllocator(&allocator);
        observing.store(true);
        bool refused = false;
        {
            const auto result =
                denoise::native_tile(input.view().as_const(), output.view(), method, budget);
            refused = !result && result.error().code == core::ErrorCode::resource;
        }
        observing.store(false);
        cv::Mat::setDefaultAllocator(original);
        if (!refused || live.load() != 0) {
            std::cerr << "Native allocation failure " << fail << "/" << observation.allocations
                      << ": resource=" << refused << "; live=" << live.load()
                      << "; attempts=" << allocation_attempts.load() << "\n";
            return 1;
        }
        ++contained;
    }
    std::cout << "{\"observed_native_peak\":" << observation.peak
              << ",\"reserved_native_bound\":" << bound << ",\"native_live_after\":" << live.load()
              << ",\"native_tile_seconds\":" << observation.seconds
              << ",\"allocation_workers\":" << worker_count
              << ",\"requested_native_threads\":" << requested_threads
              << ",\"contained_allocation_failures\":" << contained << "}\n";
    return fits ? 0 : 1;
}
} // namespace
int main() {
    try {
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
