// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <new>

namespace {
constexpr std::size_t payload_bytes = 128;
constexpr std::byte payload_pattern{0x5a};
struct Injection {
    bool armed = false;
    std::size_t calls = 0;
    std::size_t forwarded = 0;
    std::size_t refused = 0;
    std::size_t size = 0;
    std::align_val_t alignment{};
};
Injection& injection() noexcept {
    static thread_local Injection state;
    return state;
}
using AlignedNothrow = void* (*)(std::size_t, std::align_val_t, const std::nothrow_t&) noexcept;
static_assert(requires { static_cast<AlignedNothrow>(&::operator new); });
} // namespace

// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void* operator new(std::size_t bytes, std::align_val_t alignment,
                   const std::nothrow_t& /*tag*/) noexcept {
    auto& state = injection();
    ++state.calls;
    state.size = bytes;
    state.alignment = alignment;
    if (state.armed && bytes == payload_bytes &&
        alignment == std::align_val_t{docenhance::core::buffer_alignment}) {
        state.armed = false;
        ++state.refused;
        return nullptr;
    }
    ++state.forwarded;
    try {
        // The real throwing aligned family supplies storage paired with the real aligned delete.
        return ::operator new(bytes, alignment);
    } catch (...) {
        // The standard nothrow boundary also contains an arbitrary throwing new_handler.
        return nullptr;
    }
}

namespace {
bool healthy(docenhance::core::Budget& budget) {
    const auto baseline = budget.used();
    const auto forwarded = injection().forwarded;
    auto buffer = budget.allocate(payload_bytes);
    if (!buffer || buffer->size() != payload_bytes || budget.used() != baseline + payload_bytes ||
        injection().forwarded != forwarded + 1 || injection().size != payload_bytes ||
        injection().alignment != std::align_val_t{docenhance::core::buffer_alignment}) {
        return false;
    }
    void* address = buffer->bytes().data();
    auto space = buffer->size();
    if (std::align(docenhance::core::buffer_alignment, payload_bytes, address, space) !=
        buffer->bytes().data()) {
        return false;
    }
    std::ranges::fill(buffer->bytes(), payload_pattern);
    return std::ranges::all_of(buffer->bytes(),
                               [](std::byte byte) { return byte == payload_pattern; });
}
bool aligned_null_case() {
    docenhance::core::Budget budget{2 * payload_bytes};
    if (!healthy(budget) || budget.used() != 0) {
        return false;
    }
    const auto baseline = budget.used();
    const auto calls = injection().calls;
    const auto refused = injection().refused;
    if (budget.available() < payload_bytes) {
        return false;
    }
    injection().armed = true;
    auto result = budget.allocate(payload_bytes);
    const bool self_disarmed = !injection().armed;
    injection().armed = false;
    if (!self_disarmed || result || result.error().code != docenhance::core::ErrorCode::resource ||
        injection().calls != calls + 1 || injection().refused != refused + 1 ||
        injection().size != payload_bytes ||
        injection().alignment != std::align_val_t{docenhance::core::buffer_alignment} ||
        budget.used() != baseline || budget.peak() != payload_bytes) {
        return false;
    }
    return healthy(budget) && budget.used() == baseline;
}
} // namespace

int main() {
    try {
        if (aligned_null_case()) {
            if (std::fputs("PASS: actual core aligned-nothrow null branch and charge refund\n",
                           stdout) == EOF) {
                return 2;
            }
            return 0;
        }
    } catch (...) {
        injection().armed = false;
    }
    return std::fputs("Core aligned-nothrow null/refund control failed\n", stderr) == EOF ? 2 : 1;
}
