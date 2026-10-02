// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "allocation_observer.hpp"
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/surface.hpp"

#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {
namespace allocation = docenhance::tests::allocation_observer;
void begin(std::size_t failure = 0) {
    allocation::peak.store(0);
    allocation::allocation_attempts.store(0);
    allocation::failure_at.store(failure);
    allocation::observing.store(true);
}
bool selection() {
    // A known permutation of 1..n, adversarial to median-pivot partitioning. Build outside the
    // observation, then exercise the largest I01 lattice scratch without a second payload.
    constexpr std::size_t size = docenhance::methods::surface_sample_limit;
    constexpr std::size_t midpoint = size / 2;
    std::vector<double> scratch;
    scratch.reserve(size);
    for (std::size_t i = 1; i < midpoint; i += 2) {
        scratch.push_back(static_cast<double>(i));
        scratch.push_back(static_cast<double>(midpoint + i));
    }
    for (std::size_t i = 2; i <= size; i += 2) {
        scratch.push_back(static_cast<double>(i));
    }
    begin();
    const auto quantile = docenhance::image::nearest_rank(scratch, 0.5);
    allocation::observing.store(false);
    return quantile && *quantile == static_cast<double>(midpoint) &&
           allocation::allocation_attempts.load() == 0 && allocation::peak.load() == 0 &&
           allocation::live.load() == 0;
}
bool record_bookkeeping() {
    std::string input = "[";
    constexpr unsigned objects = 10000;
    for (unsigned i = 0; i < objects; ++i) {
        input += i == 0 ? "{}" : ",{}";
    }
    input += ']';
    begin();
    bool rejected = false;
    {
        const auto result = docenhance::bundle::read_record(std::as_bytes(std::span{input}));
        rejected = !result && result.error().code == docenhance::core::ErrorCode::input &&
                   result.error().message.contains("parser event ceiling");
    }
    allocation::observing.store(false);
    // This specific shallow fixture must not retain a key scope for each discarded object.
    // The old callback reached 9488 scopes; its vector alone exceeded this payload bound.
    constexpr std::size_t fixture_payload_limit = std::size_t{128} * 1024;
    return rejected && allocation::peak.load() <= fixture_payload_limit &&
           allocation::live.load() == 0;
}
bool record_allocation_refusal() {
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
    const std::string input = std::string(docenhance::bundle::record_max_depth - 2, '[') +
                              R"({"inner":[]})" +
                              std::string(docenhance::bundle::record_max_depth - 2, ']');
    begin();
    {
        const auto result = docenhance::bundle::read_record(std::as_bytes(std::span{input}));
        if (result || result.error().code != docenhance::core::ErrorCode::input) {
            allocation::observing.store(false);
            return false;
        }
    }
    allocation::observing.store(false);
    const auto attempts = allocation::allocation_attempts.load();
    if (attempts == 0 || allocation::live.load() != 0) {
        return false;
    }
    for (std::size_t failure = 1; failure <= attempts; ++failure) {
        begin(failure);
        bool refused = false;
        {
            const auto result = docenhance::bundle::read_record(std::as_bytes(std::span{input}));
            refused = !result && result.error().code == docenhance::core::ErrorCode::resource;
        }
        allocation::observing.store(false);
        if (!refused || allocation::live.load() != 0) {
            return false;
        }
    }
    std::cout << "PASS: record SAX/DOM allocation refusals contained: " << attempts << '\n';
#endif
    return true;
}
bool ledger_refusal() {
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
    namespace core = docenhance::core;
    begin(1);
    bool refused = false;
    {
        core::Budget unavailable{64};
        // The ledger control allocation failed; no nonempty charge can be admitted.
        const auto result = unavailable.reserve(1);
        refused = !result && result.error().code == core::ErrorCode::resource &&
                  unavailable.available() == 0 && unavailable.used() == 0 &&
                  unavailable.allocate(0)->empty();
    }
    allocation::observing.store(false);
    if (!refused || allocation::live.load() != 0) {
        return false;
    }
    core::Budget budget{64};
    begin();
    {
        const auto result = budget.allocate(65);
        refused = !result && result.error().code == core::ErrorCode::resource &&
                  budget.used() == 0 && budget.available() == 64;
    }
    allocation::observing.store(false);
    if (!refused || allocation::live.load() != 0) {
        return false;
    }
    const auto exact = budget.allocate(64);
    return exact && budget.used() == 64 && budget.available() == 0;
#else
    // TSan owns the replaceable allocation ABI; native/ASan exercise allocator refusal.
    return true;
#endif
}
} // namespace
int main() {
    try {
        if (!allocation::initialize_hooks() || !record_bookkeeping() ||
            !record_allocation_refusal() || !selection() || !ledger_refusal()) {
            return 1;
        }
        std::cout << "PASS: bounded record bookkeeping and allocation-free maximum selection\n";
#if DE_ALLOCATION_SANITIZER_OBSERVATION
        std::cout << "TSan retains allocator ownership; refusal runs in native/ASan modes\n";
#else
        std::cout << "PASS: ledger-control refusal and exact-budget admission\n";
#endif
        return 0;
    } catch (...) {
        allocation::observing.store(false);
        return 1;
    }
}
