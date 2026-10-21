// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "allocation_observer.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "restoration_resource_calibration.hpp"
#include "restoration_resource_fixture.hpp"
#include "restoration_resource_observation.hpp"
#include "restoration_resource_overlap.hpp"
#include "restoration_resource_pressure.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <ios>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {
namespace tests = docenhance::tests;
constexpr unsigned shards = DE_RESTORATION_RESOURCE_SHARDS;
static_assert(shards > 1);
constexpr auto extents = std::to_array<docenhance::image::Extent>({
    {.width = 1, .height = 1},
    {.width = 1, .height = 409},
    {.width = 419, .height = 1},
    {.width = 129, .height = 97},
    {.width = 503, .height = 211},
});
unsigned shard_index(std::string_view value) {
    unsigned index = 0;
    const auto* const begin = std::to_address(value.begin());
    const auto* const end = std::to_address(value.end());
    const auto parsed = std::from_chars(begin, end, index);
    if (value.empty() || (value.size() > 1 && value.front() == '0') || parsed.ec != std::errc{} ||
        parsed.ptr != end || index >= shards) {
        throw std::invalid_argument("A canonical compiled restoration shard index is required");
    }
    return index;
}
void controls() {
    for (const auto extent : extents) {
        {
            const auto method = tests::resource_wiener();
            const auto psf = tests::resource_psf();
            const docenhance::exec::Scheduler scheduler{tests::resource_concurrency()};
            tests::restoration_warm_application(method, psf, scheduler, extent);
        }
        tests::restoration_pressure_cases(extent);
        tests::restoration_exact_budget(extent);
        tests::restoration_native_precedence(extent);
        std::cout << R"({"restoration_controls_extent":true,"width":)" << extent.width
                  << R"(,"height":)" << extent.height << "}\n";
    }
    for (const auto callers : {1U, 2U, 7U}) {
        tests::restoration_overlap_case(callers);
    }
}
void execute(const bool control, const unsigned index) {
    tests::restoration_resource_calibration();
    std::cout << R"({"restoration_resource_role":")" << (control ? "controls" : "sweep")
              << R"(","shard_index":)" << index << R"(,"shard_count":)" << shards << "}\n";
    if (control) {
        controls();
    } else {
        for (const auto extent : extents) {
            tests::restoration_observed_case(extent, index, shards);
        }
    }
    if (!tests::allocation_observer::observation_valid()) {
        throw std::logic_error("Final restoration allocation observation is invalid");
    }
    // Independent PSF every-tap/inverse/DC/scale and internal-worker numerical/phase
    // controls retain their separately registered actual owners; these are whole calls.
    std::cout << R"({"restoration_resource_complete":true,"shard_index":)" << index
              << R"(,"shard_count":)" << shards << "}\n";
}
} // namespace
int main(int argc, char* const* const argv) {
    try {
        static_cast<void>(std::ios_base::sync_with_stdio(false));
        const auto args = std::span{argv, static_cast<std::size_t>(argc)};
        if (argc == 2 && std::string_view{args.subspan(1, 1).front()} == "--controls") {
            execute(true, 0);
        } else if (argc == 3 && std::string_view{args.subspan(1, 1).front()} == "--shard") {
            execute(false, shard_index(args.subspan(2, 1).front()));
        } else {
            throw std::invalid_argument(
                "An explicit restoration --controls or --shard is required");
        }
        return 0;
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("Whole-method restoration resource failure\n", stderr));
        return 1;
    }
}
