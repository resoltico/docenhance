// Isolated lookup/reservation diagnostic; no product code or R17 timing.
#include "candidate/allocation_observer.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <span>
namespace bench {
using docenhance::tests::allocation_observer::detail::ObservedAllocation;
constexpr std::size_t capacity = 2048;
constexpr unsigned repetitions = 64;
std::array<ObservedAllocation, capacity> allocations{};
std::mutex allocation_mutex;
bool reserve_allocation(void* const pointer, std::size_t bytes, bool measured) {
    const std::scoped_lock lock{allocation_mutex};
    const auto slot = std::ranges::find_if(allocations, [](const ObservedAllocation& item) {
        return item.pointer == nullptr;
    });
    if (slot == allocations.end()) {
        return false;
    }
    *slot = {
        .pointer = pointer,
        .bytes = bytes,
        .measured = measured,
        .retiring = false,
    };
    return true;
}
using Clock = std::chrono::steady_clock;
std::size_t checksum = 0;
bool valid = true;
void lookup(const char* name, std::span<ObservedAllocation> slots, const void* pointer,
            ObservedAllocation* expected) {
    const auto start = Clock::now();
    for (unsigned i = 0; i < repetitions; ++i) {
        auto* found = docenhance::tests::allocation_observer::detail::find_allocation(slots, pointer);
        valid = valid && found == expected;
        checksum += found == nullptr ? capacity : static_cast<std::size_t>(found-slots.data());
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
    std::cout << "{\"kind\":\"lookup\",\"case\":\"" << name << "\",\"operations\":" << repetitions
              << ",\"nanoseconds\":" << elapsed << ",\"valid\":" << valid << "}\n";
}
void reserve(const char* name, void* pointer, std::size_t expected, bool success) {
    const auto start = Clock::now();
    for (unsigned i = 0; i < repetitions; ++i) {
        const bool found = reserve_allocation(pointer, 137, true);
        valid = valid && found == success;
        if (found) {
            auto& slot = allocations.at(expected);
            valid = valid && slot.pointer == pointer && slot.bytes == 137 && slot.measured && !slot.retiring;
            slot = {};
        }
        checksum += found ? expected+1 : capacity;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
    std::cout << "{\"kind\":\"reservation\",\"case\":\"" << name << "\",\"operations\":" << repetitions
              << ",\"nanoseconds\":" << elapsed << ",\"valid\":" << valid << "}\n";
}
} // namespace bench
int main() {
    using namespace bench;
    std::array<std::byte, capacity> tokens{};
    std::byte unknown{};
    std::array<ObservedAllocation, capacity> slots{};
    std::cout << "{\"diagnostic\":true,\"MSC_VER\":" << _MSC_VER << ",\"iterator_debug_level\":" << _ITERATOR_DEBUG_LEVEL << ",\"capacity\":" << capacity << "}\n";
    lookup("empty-span", {}, &unknown, nullptr);
    lookup("null-empty", slots, nullptr, nullptr);
    lookup("unknown-empty", slots, &unknown, nullptr);
    for (std::size_t i = 0; i < capacity; ++i) slots[i] = {&tokens[i],i,false,false};
    lookup("first", slots, &tokens.front(), &slots.front());
    lookup("last", slots, &tokens.back(), &slots.back());
    lookup("unknown-full", slots, &unknown, nullptr);
    slots.front().retiring = true;
    slots.back().pointer = &tokens.front();
    lookup("retired-first-live-last", slots, &tokens.front(), &slots.back());
    slots.back().retiring = true;
    lookup("retired-only", slots, &tokens.front(), nullptr);
    reserve("all-null-first", &unknown, 0, true);
    for (std::size_t i = 0; i < capacity; ++i) allocations[i] = {&tokens[i],i,false,false};
    reserve("all-full", &unknown, 0, false);
    allocations.back() = {};
    reserve("last-free", &unknown, capacity-1, true);
    allocations[777] = {};
    reserve("first-free-before-last", &unknown, 777, true);
    std::cout << "{\"complete\":true,\"valid\":" << valid << ",\"checksum\":" << checksum << "}\n";
    return valid ? 0 : 3;
}
