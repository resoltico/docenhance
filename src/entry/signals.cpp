// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "signals.hpp"

#include "docenhance/core/cancellation.hpp"

#include <atomic>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // NOLINT(misc-include-cleaner)
#else
#include <array>
#include <cstddef>
#include <signal.h> // NOLINT(modernize-deprecated-headers): POSIX sigaction provider.
#endif

namespace docenhance::entry {
namespace {
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool>& interrupt_latch() noexcept {
    // Constant initialization, no guard or dynamic initialization even on first handler access.
    static constinit std::atomic<bool> pending{false};
    return pending;
}
bool pending_interrupt(core::Checkpoint /*at*/) noexcept {
    return interrupt_latch().load(std::memory_order_relaxed);
}
#ifdef _WIN32
BOOL WINAPI handle_console(DWORD event) noexcept {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) {
        return FALSE;
    }
    interrupt_latch().store(true, std::memory_order_relaxed);
    return TRUE;
}
#else
constexpr auto handled_signals = std::to_array<int>({SIGINT, SIGTERM});
extern "C" void handle_signal(int /*signal*/) noexcept {
    // No allocation, callbacks, I/O, mutexes, exception handling or source/token destruction.
    interrupt_latch().store(true, std::memory_order_relaxed);
}
#endif
} // namespace
core::Cancellation process_cancellation() noexcept {
    return core::Cancellation{{}, pending_interrupt};
}
bool InterruptScope::install() noexcept {
#ifdef _WIN32
    if (installed_) {
        return false;
    }
    installed_ = SetConsoleCtrlHandler(handle_console, TRUE) != 0;
    return installed_;
#else
    struct sigaction action{};
    action.sa_handler = handle_signal;
    action.sa_flags = SA_RESTART;
    if (sigemptyset(&action.sa_mask) != 0) {
        return false;
    }
    for (const auto signal : handled_signals) {
        if (sigaddset(&action.sa_mask, signal) != 0) {
            return false;
        }
    }
    for (std::size_t i = 0; i < handled_signals.size(); ++i) {
        if (installed_.at(i) || sigaction(handled_signals.at(i), nullptr, &previous_.at(i)) != 0) {
            return false;
        }
        // Respect dispositions inherited from a shell that deliberately ignored interruption.
        if (previous_.at(i).sa_handler == SIG_IGN) {
            continue;
        }
        if (sigaction(handled_signals.at(i), &action, nullptr) != 0) {
            return false;
        }
        installed_.at(i) = true;
    }
    return true;
#endif
}
InterruptScope::~InterruptScope() {
#ifdef _WIN32
    if (installed_) {
        static_cast<void>(SetConsoleCtrlHandler(handle_console, FALSE));
    }
#else
    for (std::size_t i = 0; i < handled_signals.size(); ++i) {
        if (installed_.at(i)) {
            static_cast<void>(sigaction(handled_signals.at(i), &previous_.at(i), nullptr));
        }
    }
#endif
}
} // namespace docenhance::entry
