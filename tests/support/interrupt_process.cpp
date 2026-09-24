// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Process test: same production interrupt bridge/CLI/host, but readiness precedes dispatch.
// No testing option or progress output is added to the shipped executable.
#include "docenhance/cli/run.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/host/processor.hpp"
#include "signals.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <charconv>
#include <system_error>
#include <windows.h>
#else
#include <csignal>
#endif

namespace {
#ifndef _WIN32
extern "C" void prior_handler(int /*signal*/) noexcept {}
int check_dispositions() {
    const auto old_interrupt = std::signal(SIGINT, SIG_IGN);
    const auto old_term = std::signal(SIGTERM, prior_handler);
    if (old_interrupt == SIG_ERR || old_term == SIG_ERR) {
        return 2;
    }
    {
        docenhance::entry::InterruptScope scope;
        if (!scope.install() || std::raise(SIGINT) != 0 ||
            docenhance::entry::process_cancellation().requested(
                docenhance::core::Checkpoint::admission)) {
            return 2;
        }
    }
    const auto restored_term = std::signal(SIGTERM, old_term);
    const auto restored_interrupt = std::signal(SIGINT, old_interrupt);
    return restored_term == prior_handler && restored_interrupt == SIG_IGN ? 0 : 2;
}
#endif
#ifdef _WIN32
int send_break(std::string_view process) {
    unsigned long id = 0;
    const auto parsed = std::from_chars(process.begin(), process.end(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != process.end() || id == 0) {
        return 2;
    }
    static_cast<void>(FreeConsole());
    if (AttachConsole(id) == 0) {
        return 2;
    }
    const bool sent = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, id) != 0;
    static_cast<void>(FreeConsole());
    return sent ? 0 : 2;
}
#endif
int run(std::span<char* const> args) {
    constexpr std::size_t arguments = 3;
    if (args.size() != arguments) {
        return 2;
    }
#ifdef _WIN32
    if (std::string_view{args.subspan(1).front()} == "--send") {
        return send_break(args.subspan(2).front());
    }
#else
    if (std::string_view{args.subspan(1).front()} == "--dispositions") {
        return check_dispositions();
    }
#endif
#ifdef _WIN32
    // Hosted CI may have no console. Allocate before installing the production handler.
    if (GetConsoleCP() == 0 && AllocConsole() == 0) {
        return 2;
    }
#endif
    docenhance::entry::InterruptScope interrupts;
    if (!interrupts.install()) {
        return 2;
    }
    const auto control = docenhance::entry::process_cancellation();
    std::cout << "READY\n" << std::flush;
    if (std::cin.get() != '\n') {
        return 2;
    }
    // Synchronization is the observed handler latch, never an assumed sleep duration.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!control.requested(docenhance::core::Checkpoint::admission)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return 2;
        }
        std::this_thread::yield();
    }
    const auto invocation = std::to_array<const char*>({
        "docenhance",
        "process",
        args.subspan(1).front(),
        "--out-dir",
        args.subspan(2).front(),
        "--binarize",
        "fixed",
        "--json",
    });
    docenhance::host::Processor processor;
    return docenhance::cli::run(invocation, processor, std::cout, std::cerr, control);
}
} // namespace
int main(int argc, char** const argv) { // NOLINT(misc-const-correctness)
    try {
        return run({argv, static_cast<std::size_t>(argc)});
    } catch (...) {
        return 2;
    }
}
