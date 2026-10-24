// Finite constructor exception diagnostic, not product execution or performance evidence.
#include "docenhance/core/result.hpp"
#define POCKETFFT_NO_MULTITHREADING
#define POCKETFFT_CACHE_SIZE 0
#include "pocketfft_hdronly.h"
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <utility>
#include <vector>
namespace probe {
std::atomic<bool> active{false};
std::atomic<std::size_t> attempts{0}, failure{0}, last_bytes{0};
const char* current = "startup";
std::size_t sink = 0;
void report(const char* outcome) {
    std::printf("{\"case\":\"%s\",\"outcome\":\"%s\",\"attempts\":%zu,\"failure\":%zu,\"last_bytes\":%zu,\"sink\":%zu}\n",
                current, outcome, attempts.load(), failure.load(), last_bytes.load(), sink);
    std::fflush(stdout);
}
void terminated() noexcept {
    active.store(false);
    report("terminate");
    std::_Exit(86);
}
void run(const char* name, std::size_t ordinal) {
    // Inputs are created before enabling the one-shot refusal; constructor work is isolated.
    std::vector<int> input(0);
    pocketfft::detail::shape_t shape(1);
    pocketfft::detail::stride_t stride(1);
    current = name;
    attempts.store(0);
    last_bytes.store(0);
    failure.store(ordinal);
    active.store(true);
    bool caught = false;
    try {
        if (std::strcmp(name, "error-default") == 0) {
            const docenhance::core::Error value{};
            sink += value.message.size();
        } else if (std::strcmp(name, "vector-default") == 0) {
            const std::vector<int> value;
            sink += value.size();
        } else if (std::strcmp(name, "vector-move") == 0) {
            const std::vector<int> value(std::move(input));
            sink += value.size();
        } else if (std::strcmp(name, "vector-count0") == 0) {
            const std::vector<int> value(0);
            sink += value.size();
        } else if (std::strcmp(name, "vector-copy") == 0) {
            const std::vector<int> value(input);
            sink += value.size();
        } else if (std::strcmp(name, "cfftp-length1") == 0) {
            const pocketfft::detail::cfftp<double> value(1);
            static_cast<void>(value);
        } else if (std::strcmp(name, "rfftp-length1") == 0) {
            const pocketfft::detail::rfftp<double> value(1);
            static_cast<void>(value);
        } else if (std::strcmp(name, "arr-info-moves") == 0) {
            const pocketfft::detail::arr_info value(std::move(shape), std::move(stride));
            sink += value.ndim();
        } else {
            active.store(false);
            std::_Exit(90);
        }
    } catch (const std::bad_alloc&) {
        caught = true;
    }
    active.store(false);
    report(caught ? "caught_bad_alloc" : "returned");
}
} // namespace probe
void* operator new(std::size_t size) {
    if (probe::active.load()) {
        probe::last_bytes.store(size);
        const auto ordinal = probe::attempts.fetch_add(1) + 1;
        if (ordinal == probe::failure.load()) {
            throw std::bad_alloc{};
        }
    }
    void* pointer = std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) {
        throw std::bad_alloc{};
    }
    return pointer;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
int main(int argc, char** argv) {
    std::set_terminate(probe::terminated);
#ifdef _MSC_VER
    std::printf("{\"MSC_VER\":%d,\"iterator_debug_level\":%d,\"DEBUG\":%d}\n", _MSC_VER,
                _ITERATOR_DEBUG_LEVEL,
#ifdef _DEBUG
                1
#else
                0
#endif
    );
#endif
    if (argc == 1) {
        for (const char* name : {"error-default", "vector-default", "vector-move", "vector-count0",
                                 "vector-copy", "cfftp-length1", "rfftp-length1", "arr-info-moves"}) {
            probe::run(name, 0);
        }
        return 0;
    }
    if (argc != 2) { return 91; }
    probe::run(argv[1], 1);
    return 0;
}
