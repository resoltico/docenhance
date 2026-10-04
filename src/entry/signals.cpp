// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
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
#include <ranges>
#include <signal.h> // NOLINT(modernize-deprecated-headers): POSIX sigaction provider.
#ifdef __APPLE__
#include <sys/signal.h>
#endif
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
BOOL WINAPI handle_console(DWORD event) noexcept {            // NOLINT(misc-include-cleaner)
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) { // NOLINT(misc-include-cleaner)
        return 0;
    }
    interrupt_latch().store(true, std::memory_order_relaxed);
    return 1;
}
#else
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
    installed_ = SetConsoleCtrlHandler(handle_console, 1) != 0; // NOLINT(misc-include-cleaner)
    return installed_;
#else
    struct sigaction action{};
    action.sa_handler = handle_signal;
    action.sa_flags = SA_RESTART;
    if (sigemptyset(&action.sa_mask) != 0) {
        return false;
    }
    for (const auto signal : managed_signals) {
        if (sigaddset(&action.sa_mask, signal) != 0) {
            return false;
        }
    }
    for (auto [signal, previous, installed] :
         std::views::zip(managed_signals, previous_, installed_)) {
        if (installed || sigaction(signal, nullptr, &previous) != 0) {
            return false;
        }
        // Respect dispositions inherited from a shell that deliberately ignored interruption.
        if (previous.sa_handler == SIG_IGN) {
            continue;
        }
        // Broken response pipes are stream failures, never cancellation or signal exit.
        action.sa_handler = signal == SIGPIPE ? SIG_IGN : handle_signal;
        if (sigaction(signal, &action, nullptr) != 0) {
            return false;
        }
        installed = true;
    }
    return true;
#endif
}
InterruptScope::~InterruptScope() {
#ifdef _WIN32
    if (installed_) {
        static_cast<void>(SetConsoleCtrlHandler(handle_console, 0));
    }
#else
    for (auto [signal, previous, installed] :
         std::views::zip(managed_signals, previous_, installed_)) {
        if (installed) {
            static_cast<void>(sigaction(signal, &previous, nullptr));
        }
    }
#endif
}
} // namespace docenhance::entry
