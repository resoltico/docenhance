// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "allocation_observer.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/core/result.hpp"
#include "failures.hpp"

#include <iostream>
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
#include "docenhance/app/verify.hpp"
#include "docenhance/cli/run.hpp"
#include "docenhance/core/cancellation.hpp"
#include "processor.hpp"
#include "stub_verifier.hpp"

#include <array>
#include <sstream>
#endif
namespace {
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
class CountingVerifier final : public docenhance::app::Verifier {
  public:
    unsigned calls = 0;
    docenhance::core::Result<docenhance::app::Verified>
    verify(const docenhance::app::VerifyRequest& /*request*/,
           const docenhance::core::Cancellation& /*cancellation*/) override {
        ++calls;
        return docenhance::core::failure(docenhance::core::ErrorCode::input, "Unexpected verify");
    }
};
bool check_unparsed_allocation_transport() {
    namespace allocation = docenhance::tests::allocation_observer;
    docenhance::tests::RejectingProcessor processor;
    CountingVerifier verifier;
    std::ostringstream out;
    std::ostringstream err;
    const auto arguments = std::to_array<const char*>({
        "docenhance",
        "process",
        "--out-dir",
        "--json",
    });
    allocation::allocation_attempts.store(0);
    allocation::failure_at.store(1);
    allocation::persistent_failure.store(false);
    allocation::observing.store(true);
    int code = -1;
    try {
        code = docenhance::cli::run(arguments, {.processor = processor, .verifier = verifier}, out,
                                    err);
    } catch (...) {
        allocation::observing.store(false);
        return false;
    }
    allocation::observing.store(false);
    const bool valid =
        code == static_cast<int>(docenhance::core::ExitCode::processing) &&
        allocation::allocation_attempts.load() > 0 && processor.calls == 0 && verifier.calls == 0 &&
        out.str().empty() &&
        err.str() ==
            "E_RESOURCE: The system refused an allocation\n"
            "Publication: not_started\n"
            "Publication did not start; correct the reported problem before retrying.\n"
            "An incomplete response or delivery failure does not prove absence of effects.\n";
    if (!valid) {
        std::cerr << "Unparsed allocation transport: code=" << code
                  << ", attempts=" << allocation::allocation_attempts.load()
                  << ", processor=" << processor.calls << ", verifier=" << verifier.calls
                  << ", stdout=" << out.str() << ", stderr=" << err.str() << '\n';
    }
    return valid;
}
#endif
int check_allocation_boundary() {
    namespace allocation = docenhance::tests::allocation_observer;
    if (!allocation::initialize_hooks()) {
        return 1;
    }
    const docenhance::app::Invocation invocation;
    allocation::peak.store(0);
    allocation::allocation_attempts.store(0);
    allocation::failure_at.store(1);
    allocation::persistent_failure.store(true);
    allocation::observing.store(true);
    const auto fallback = docenhance::cli::allocation_failure(invocation);
    allocation::observing.store(false);
    if (fallback.exit_code() != docenhance::core::ExitCode::processing ||
        allocation::allocation_attempts.load() != 0 || allocation::peak.load() != 0) {
        std::cerr << "Allocation-free fallback failed its code/attempt/peak assertions\n";
        return 1;
    }
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
    if (!check_unparsed_allocation_transport()) {
        return 1;
    }
    docenhance::tests::RejectingProcessor processor;
    docenhance::tests::RefusingVerifier verifier;
    std::ostringstream out;
    std::ostringstream err;
    // Known invalid argv0 selects syntax-error JSON before any fallible parsing allocation.
    // Keep persistent render refusal independent of Darwin ASan's libc++ string interposition.
    const auto arguments = std::to_array<const char*>({"\xff", "version", "--json"});
    allocation::allocation_attempts.store(0);
    allocation::failure_at.store(1);
    allocation::persistent_failure.store(true);
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
        std::cerr << "Persistent allocation refusal: code=" << code
                  << ", attempts=" << allocation::allocation_attempts.load()
                  << ", processor=" << processor.calls << ", stdout=" << out.str()
                  << ", stderr=" << err.str() << '\n';
        return 1;
    }
    std::cout
        << "PASS: persistent allocation refusal stays inside CLI and cannot fabricate delivery\n";
#else
    std::cout << "PASS: TSan observes an allocation-free handler; native/ASan exercise "
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
