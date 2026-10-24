// Finite actual failed(resource) propagation control, not a product or performance test.
#include "fft_detail.hpp"
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
namespace probe {
std::atomic<bool> active{false};
std::atomic<std::size_t> attempts{0}, last_bytes{0};
std::atomic<bool> reached{false};
void terminated() noexcept {
    active.store(false);
    std::printf("{\"outcome\":\"terminate\",\"attempts\":%zu}\n", attempts.load());
    std::fflush(stdout);
    std::_Exit(86);
}
docenhance::core::Result<void> actual_failure() {
    try {
        for (unsigned ordinal = 0; ordinal < 3; ++ordinal) {
            auto* pointer = ::operator new(16);
            ::operator delete(pointer);
        }
    } catch (const std::bad_alloc&) {
        return docenhance::fft::failed(docenhance::core::ErrorCode::resource);
    }
    return {};
}
} // namespace probe
void* operator new(std::size_t bytes) {
    if (probe::active.load()) {
        probe::last_bytes.store(bytes);
        if (probe::attempts.fetch_add(1) + 1 == 3) {
            probe::reached.store(true);
            throw std::bad_alloc{};
        }
    }
    auto* pointer = std::malloc(bytes == 0 ? 1 : bytes);
    if (pointer == nullptr) { throw std::bad_alloc{}; }
    return pointer;
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
int main() {
    std::set_terminate(probe::terminated);
    bool resource = false;
    std::size_t after_return = 0;
    probe::active.store(true);
    {
        const auto result = probe::actual_failure();
        resource = !result && result.error().code == docenhance::core::ErrorCode::resource;
        after_return = probe::attempts.load();
    }
    probe::active.store(false);
#ifdef _MSC_VER
    constexpr int compiler = _MSC_VER, iterator_level = _ITERATOR_DEBUG_LEVEL;
#else
    constexpr int compiler = 0, iterator_level = 0;
#endif
    std::printf("{\"outcome\":\"returned_resource\",\"MSC_VER\":%d,\"iterator_debug_level\":%d,\"refused_ordinal\":3,\"reached\":%d,\"resource\":%d,\"attempts_after_return\":%zu,\"attempts_after_destruction\":%zu,\"last_bytes\":%zu}\n",
                compiler, iterator_level, probe::reached.load(), resource, after_return,
                probe::attempts.load(), probe::last_bytes.load());
    std::fflush(stdout);
    return resource && probe::reached.load() && after_return > 3 ? 0 : 1;
}
