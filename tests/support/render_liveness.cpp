// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "support/entry_point.hpp"
#include "support/oracle.hpp"
#include "support/render_receipt.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <ios>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> read_seed(const char* const path) {
    using docenhance::fuzz::require;
    std::ifstream input{path, std::ios::binary};
    input.seekg(0, std::ios::end);
    require(input.is_open() && input.good(), "liveness tracked seed opens");
    const std::streamoff size = input.tellg();
    require(size > 0 && size <= 6252, "liveness seed stays in finite envelope domain");
    input.seekg(0);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(size));
    for (std::streamoff i = 0; i < size; ++i) {
        const auto byte = input.get();
        require(byte != std::char_traits<char>::eof(), "liveness seed complete bounded read");
        bytes.push_back(static_cast<std::uint8_t>(byte));
    }
    require(input.get() == std::char_traits<char>::eof() && !input.bad(),
            "liveness seed exact EOF");
    return bytes;
}
int liveness_run(std::span<char* const> arguments) {
    using docenhance::fuzz::require;
    require(arguments.size() == 4, "three unique tracked liveness controls are required");
    const auto before = docenhance::fuzz::render_harness_counts();
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const auto bytes = read_seed(arguments.subspan(i, 1).front());
        require(LLVMFuzzerTestOneInput(bytes.data(), bytes.size()) == 0,
                "actual shared render callback returns");
    }
    const auto after = docenhance::fuzz::render_harness_counts();
    require(after.entry_calls - before.entry_calls == 3, "actual callback entry delta3");
    require(after.render_calls - before.render_calls == 3, "actual render delta3");
    require(after.healthy_completions - before.healthy_completions == 1,
            "actual healthy completion delta1");
    require(after.output_comparisons - before.output_comparisons == 1,
            "actual independent native comparison delta1");
    require(after.input_refusals - before.input_refusals == 1,
            "actual malformed-source codec refusal delta1");
    require(after.moved_refusals - before.moved_refusals == 1,
            "actual moved-before-stop refusal delta1");
    require(after.refunded_cases - before.refunded_cases == 3, "actual complete-refund delta3");
    std::cout << "{\"entry_calls\":" << after.entry_calls - before.entry_calls
              << ",\"render_calls\":" << after.render_calls - before.render_calls
              << ",\"healthy_completions\":"
              << after.healthy_completions - before.healthy_completions
              << ",\"output_comparisons\":" << after.output_comparisons - before.output_comparisons
              << ",\"input_refusals\":" << after.input_refusals - before.input_refusals
              << ",\"moved_refusals\":" << after.moved_refusals - before.moved_refusals
              << ",\"refunded_cases\":" << after.refunded_cases - before.refunded_cases << "}\n";
    return 0;
}
} // namespace
// NOLINTNEXTLINE(misc-const-correctness)
int main(int argc, char** const argv) {
    try {
        return liveness_run({argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
    } catch (...) {
        static_cast<void>(std::fputs("Non-standard liveness control failure\n", stderr));
    }
    return 1;
}
