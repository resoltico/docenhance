// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Process test: same production interrupt bridge/CLI/host, but readiness precedes dispatch.
// No testing option or progress output is added to the shipped executable.
#include "docenhance/cli/run.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/host/verifier.hpp"
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
#include <cstdint>
#include <system_error>
#include <windows.h> // NOLINT(misc-include-cleaner)
#else
#include <csignal>
#include <signal.h> // NOLINT(modernize-deprecated-headers): POSIX sigaction provider.
#ifdef __APPLE__
#include <sys/signal.h>
#endif
#endif

namespace {
#ifndef _WIN32
extern "C" void prior_handler(int /*signal*/) noexcept {}
// The bridge saves and restores sigaction dispositions, so this check reads them back the same
// way. Asking through std::signal would compare against whatever a second API reports, which
// under an interposing runtime is that runtime's own wrapper rather than the restored handler.
bool set_disposition(int signal, void (*const handler)(int)) noexcept {
    struct sigaction action{};
    action.sa_handler = handler;
    return sigemptyset(&action.sa_mask) == 0 && sigaction(signal, &action, nullptr) == 0;
}
bool disposition_is(int signal, void (*const handler)(int)) noexcept {
    struct sigaction current{};
    return sigaction(signal, nullptr, &current) == 0 && current.sa_handler == handler;
}
int check_dispositions() {
    if (!set_disposition(SIGINT, SIG_IGN) || !set_disposition(SIGTERM, prior_handler) ||
        !set_disposition(SIGPIPE, prior_handler)) {
        return 2;
    }
    {
        docenhance::entry::InterruptScope scope;
        if (!scope.install() || !disposition_is(SIGPIPE, SIG_IGN) || std::raise(SIGPIPE) != 0 ||
            std::raise(SIGINT) != 0 ||
            docenhance::entry::process_cancellation().requested(
                docenhance::core::Checkpoint::admission)) {
            return 2;
        }
    }
    return disposition_is(SIGTERM, prior_handler) && disposition_is(SIGINT, SIG_IGN) &&
                   disposition_is(SIGPIPE, prior_handler)
               ? 0
               : 2;
}
#endif
#ifdef _WIN32
int send_break(std::string_view process) {
    if (process.empty()) {
        return 2;
    }
    std::uint32_t id = 0;
    // MSVC string_view iterators are not raw pointers; an empty suffix supplies the end pointer.
    const char* const end = process.substr(process.size()).data();
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    const auto parsed = std::from_chars(process.data(), end, id);
    if (parsed.ec != std::errc{} || parsed.ptr != end || id == 0) {
        return 2;
    }
    static_cast<void>(FreeConsole()); // NOLINT(misc-include-cleaner)
    if (AttachConsole(id) == 0) {     // NOLINT(misc-include-cleaner)
        return 2;
    }
    const bool sent =
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, id) != 0; // NOLINT(misc-include-cleaner)
    static_cast<void>(FreeConsole());                        // NOLINT(misc-include-cleaner)
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
    if (GetConsoleCP() == 0 && AllocConsole() == 0) { // NOLINT(misc-include-cleaner)
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
    docenhance::host::Processor processor;
    docenhance::host::Verifier verifier;
    if (std::string_view{args.subspan(1).front()} == "--verify") {
        const auto invocation = std::to_array<const char*>({
            "docenhance",
            "verify",
            args.subspan(2).front(),
            "--json",
        });
        return docenhance::cli::run(invocation, {.processor = processor, .verifier = verifier},
                                    std::cout, std::cerr, control);
    }
    const auto invocation = std::to_array<const char*>({
        "docenhance",
        "process",
        args.subspan(1).front(),
        "--out-dir",
        args.subspan(2).front(),
        "--output-mode",
        "bw",
        "--binarize",
        "fixed",
        "--json",
    });
    return docenhance::cli::run(invocation, {.processor = processor, .verifier = verifier},
                                std::cout, std::cerr, control);
}
} // namespace
int main(int argc, char** const argv) { // NOLINT(misc-const-correctness)
    try {
        return run({argv, static_cast<std::size_t>(argc)});
    } catch (...) {
        return 2;
    }
}
