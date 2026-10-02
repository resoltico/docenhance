// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "allocation_observer.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "failures.hpp"

#include <iostream>
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
#include "docenhance/cli/run.hpp"
#include "processor.hpp"
#include "stub_verifier.hpp"

#include <array>
#include <sstream>
#endif
namespace {
int check_allocation_boundary() {
    namespace allocation = docenhance::tests::allocation_observer;
    if (!allocation::initialize_hooks()) {
        return 1;
    }
    const docenhance::contract::Invocation invocation;
    allocation::peak.store(0);
    allocation::allocation_attempts.store(0);
    allocation::failure_at.store(1);
    allocation::persistent_failure.store(true);
    allocation::observing.store(true);
    const auto fallback = docenhance::cli::allocation_failure(invocation);
    allocation::observing.store(false);
    if (fallback.exit_code() != docenhance::core::ExitCode::processing ||
        allocation::allocation_attempts.load() != 0 || allocation::peak.load() != 0) {
        return 1;
    }
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
    docenhance::tests::RejectingProcessor processor;
    docenhance::tests::RefusingVerifier verifier;
    std::ostringstream out;
    std::ostringstream err;
    const auto arguments = std::to_array<const char*>({"docenhance", "version", "--json"});
    allocation::observing.store(true);
    int code = -1;
    try {
        code = docenhance::cli::run(arguments, {.processor = processor, .verifier = verifier}, out,
                                    err);
    } catch (...) {
        allocation::observing.store(false);
        return 1;
    }
    allocation::observing.store(false);
    if (code != static_cast<int>(docenhance::core::ExitCode::output) || processor.calls != 0 ||
        allocation::allocation_attempts.load() == 0 || !out.str().empty() || !err.str().empty()) {
        return 1;
    }
    std::cout
        << "PASS: persistent allocation refusal stays inside CLI and cannot fabricate delivery\n";
#else
    std::cout << "PASS: TSan observes an allocation-free handler; native/ASan own "
                 "persistent-refusal integration\n";
#endif
    return 0;
}

} // namespace
int main() {
    try {
        return check_allocation_boundary();
    } catch (...) {
        docenhance::tests::allocation_observer::observing.store(false);
        return 1;
    }
}
