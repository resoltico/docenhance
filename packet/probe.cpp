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
#include <memory>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif
#include <utility>
#include <vector>
namespace probe {
std::atomic<bool> active{false};
std::atomic<std::size_t> attempts{0}, failure{0}, last_bytes{0};
const char* current = "startup";
std::size_t sink = 0;
bool release_unknown = false;
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
void release(void* pointer) noexcept {
#ifdef _MSC_VER
    if (release_unknown) { _free_dbg(pointer, _UNKNOWN_BLOCK); return; }
#endif
    std::free(pointer);
}
#ifdef _MSC_VER
int client_report(int type, char*, int*) {
    if (type == _CRT_ASSERT || type == _CRT_ERROR) {
        active.store(false);
        report(type == _CRT_ASSERT ? "CRT_assert" : "CRT_error");
        std::_Exit(87);
    }
    return 0;
}
void client_case(const char* name) {
    current = name;
    release_unknown = std::strcmp(name, "client-unknown") == 0;
    _CrtSetReportHook(client_report);
    attempts.store(0); failure.store(0); last_bytes.store(0); active.store(true);
    std::unique_ptr<std::byte> pointer{new (_CLIENT_BLOCK, __FILE__, __LINE__) std::byte{}};
    active.store(false);
    const int tag = _CrtReportBlockType(pointer.get());
    std::printf("{\"case\":\"%s\",\"phase\":\"before_delete\",\"tag\":%d,\"expected_tag\":%d,\"ordinary_attempts\":%zu}\n", name, tag, _CLIENT_BLOCK, attempts.load());
    std::fflush(stdout);
    if (tag != _CLIENT_BLOCK || attempts.load() != 0) { std::_Exit(92); }
    active.store(true);
    pointer.reset();
    active.store(false);
    if (attempts.load() != 0) { std::_Exit(94); }
    if (_CrtCheckMemory() == 0) { std::_Exit(93); }
    report("client_released");
}
#endif
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
void operator delete(void* pointer) noexcept { probe::release(pointer); }
void operator delete[](void* pointer) noexcept { probe::release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { probe::release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { probe::release(pointer); }
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
#ifdef _MSC_VER
    if (argc == 2 && (std::strcmp(argv[1], "client-normal") == 0 || std::strcmp(argv[1], "client-unknown") == 0)) {
        probe::client_case(argv[1]);
        return 0;
    }
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
