// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Deliberate faults in isolated test processes; never linked into product execution.
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <latch>
#include <memory>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>

namespace {
void observed(int value) noexcept {
    static_cast<void>(std::fwrite(&value, sizeof(value), 1, stdout));
}
int memory(const char* const index_text) {
    constexpr std::size_t extent = 8;
    const auto values = std::make_unique<std::array<int, extent>>();
    const auto index = static_cast<std::size_t>(std::strtoul(index_text, nullptr, 10));
    // Unchecked access is the ASan negative control, not production container policy.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    observed(values->data()[index]);
    return 0;
}
int arithmetic(const char* const value_text) {
    const std::string_view text{value_text};
    int value = 0;
    // from_chars takes a raw bounded range; checked MSVC string-view iterators are not pointers.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto* const end = value_text + text.size();
    const auto parsed = std::from_chars(value_text, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return 2;
    }
    observed(value + 1);
    return 0;
}
int race() {
    int shared = 0;
    std::latch ready{2};
    auto first = std::thread{[&] {
        ready.arrive_and_wait();
        shared = 1;
    }};
    auto second = std::thread{[&] {
        ready.arrive_and_wait();
        shared = 2;
    }};
    first.join();
    second.join();
    observed(shared);
    return 0;
}
int probe(std::span<char* const> args) {
    if (args.size() != 3) {
        return 2;
    }
    const std::string_view mode{args.subspan(1, 1).front()};
    const auto* const value = args.subspan(2, 1).front();
    if (mode == "memory") {
        return memory(value);
    }
    if (mode == "arithmetic") {
        return arithmetic(value);
    }
    if (mode == "race") {
        return race();
    }
    return 2;
}
} // namespace
int main(int argc, char** const argv) {
    try {
        return probe({argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
    } catch (...) {
        static_cast<void>(std::fputs("Probe failed before its intended diagnostic\n", stderr));
    }
    return 2;
}
