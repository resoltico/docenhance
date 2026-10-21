// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace docenhance::tests {
namespace {
constexpr std::uint32_t hierarchy_columns = 32;
constexpr std::uint32_t hierarchy_rows = 8;
constexpr std::uint32_t hierarchy_cells = hierarchy_columns * hierarchy_rows;
std::size_t hierarchy_resident_bytes() {
    return image::plane_bytes(image::plane_shape(hierarchy_cells, 7, sizeof(double)).value())
               .value() +
           image::plane_bytes(image::plane_shape(hierarchy_cells, 1, sizeof(double)).value())
               .value();
}
std::size_t hierarchy_storage_bytes() {
    std::uint32_t columns = hierarchy_columns;
    std::uint32_t rows = hierarchy_rows;
    std::uint32_t count = 0;
    while (true) {
        count += columns * rows;
        if (columns == 1 && rows == 1) {
            break;
        }
        columns = (columns + 1) / 2;
        rows = (rows + 1) / 2;
    }
    return image::plane_bytes(image::plane_shape(count, 6, sizeof(double)).value()).value();
}
struct HierarchyFixture {
    explicit HierarchyFixture(std::size_t extra)
        : budget(hierarchy_resident_bytes() + extra),
          work(image::Plane<double>::allocate(budget, hierarchy_cells, 7).value()),
          logarithms(image::Plane<double>::allocate(budget, hierarchy_cells, 1).value()) {
        std::ranges::fill(work.view().storage(), 0);
        std::ranges::fill(logarithms.view().storage(), -7);
        constexpr auto measured = hierarchy_cells / 4;
        for (std::uint32_t i = 0; i < measured; ++i) {
            work.view().row(0).subspan(i, 1).front() = 1;
            work.view().row(1).subspan(i, 1).front() = -1 - (static_cast<double>(i) / measured);
        }
    }
    [[nodiscard]] methods::SurfaceSystem system() const {
        return {
            .grid =
                {
                    .extent = {.width = hierarchy_columns, .height = hierarchy_rows},
                    .columns = hierarchy_columns,
                    .rows = hierarchy_rows,
                },
            .weights = work.view().row(0),
            .smooth = 2,
        };
    }
    core::Budget budget;
    image::Plane<double> work;
    image::Plane<double> logarithms;
};
void hierarchy_exact_case() {
    const auto bytes = hierarchy_storage_bytes();
    HierarchyFixture fixture{bytes};
    const auto resident = fixture.budget.used();
    const auto weights = gamma_bits(fixture.work.view().row(0));
    const auto measured = gamma_bits(fixture.work.view().row(1));
    const auto system = fixture.system();
    {
        const auto hierarchy = methods::SurfaceHierarchy::build(system, fixture.budget);
        require(hierarchy && hierarchy->levels().size() == 6 &&
                    hierarchy->levels().back().columns == 1 &&
                    hierarchy->levels().back().rows == 1 &&
                    fixture.budget.used() == resident + bytes,
                "actual six-level hierarchy owns its complete aligned343-cell six-vector storage");
    }
    require(fixture.budget.used() == resident, "direct hierarchy owner refunds its actual payload");
    const CheckpointStop observe{core::Checkpoint::solving,
                                 std::numeric_limits<std::size_t>::max()};
    const auto cancellation = observe.cancellation();
    methods::SolverReport report;
    const auto result =
        methods::solve_surface(system, fixture.work.view(), fixture.logarithms.view().row(0),
                               report, {.budget = fixture.budget, .cancellation = cancellation});
    require(result && report.iterations > 1 && report.residual <= report.tolerance &&
                fixture.budget.peak() == resident + bytes && fixture.budget.used() == resident &&
                gamma_bits(fixture.work.view().row(0)) == weights &&
                gamma_bits(fixture.work.view().row(1)) == measured,
            "actual nonconstant healthy solve enters hierarchy, meets true residual and restores "
            "resident inputs/charge");
    std::cout << "I01_HIERARCHY_HEALTHY bytes=" << bytes << " resident=" << resident
              << " iterations=" << report.iterations << " residual=" << report.residual
              << " tolerance=" << report.tolerance << " visits=" << CheckpointStop::visits()
              << '\n';
}
void hierarchy_short_case() {
    const auto bytes = hierarchy_storage_bytes();
    HierarchyFixture fixture{bytes - 1};
    const auto resident = fixture.budget.used();
    const auto weights = gamma_bits(fixture.work.view().row(0));
    const auto measured = gamma_bits(fixture.work.view().row(1));
    const auto system = fixture.system();
    const core::Cancellation cancellation;
    methods::SolverReport report;
    const auto result =
        methods::solve_surface(system, fixture.work.view(), fixture.logarithms.view().row(0),
                               report, {.budget = fixture.budget, .cancellation = cancellation});
    require(!result && result.error().code == core::ErrorCode::resource && report.iterations == 0 &&
                std::isfinite(report.residual) && report.residual > report.tolerance &&
                fixture.budget.used() == resident && fixture.budget.peak() == resident &&
                gamma_bits(fixture.work.view().row(0)) == weights &&
                gamma_bits(fixture.work.view().row(1)) == measured,
            "one-byte-short actually reaches hierarchy after initial true residual exceeds "
            "tolerance; no usable solve and full refund");
    std::cout << "I01_HIERARCHY_SHORT bytes=" << bytes << " resident=" << resident
              << " residual=" << report.residual << " tolerance=" << report.tolerance << '\n';
}
void hierarchy_interior_case() {
    const auto bytes = hierarchy_storage_bytes();
    HierarchyFixture fixture{bytes};
    const auto resident = fixture.budget.used();
    const auto weights = gamma_bits(fixture.work.view().row(0));
    const auto measured = gamma_bits(fixture.work.view().row(1));
    const auto system = fixture.system();
    const CheckpointStop stop{core::Checkpoint::solving, 29};
    const auto cancellation = stop.cancellation();
    methods::SolverReport report;
    const auto result =
        methods::solve_surface(system, fixture.work.view(), fixture.logarithms.view().row(0),
                               report, {.budget = fixture.budget, .cancellation = cancellation});
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 30 && report.iterations == 1 &&
                report.residual > report.tolerance && fixture.budget.peak() == resident + bytes &&
                fixture.budget.used() == resident &&
                gamma_bits(fixture.work.view().row(0)) == weights &&
                gamma_bits(fixture.work.view().row(1)) == measured,
            "real solve completes one step then stops inside its next V-cycle and refunds actual "
            "hierarchy, not mutable scratch rollback");
    std::cout << "I01_HIERARCHY_INTERIOR visits=" << CheckpointStop::visits()
              << " iterations=" << report.iterations << " resident=" << resident
              << " refunded=" << fixture.budget.used() << '\n';
}
} // namespace
TEST_CASE(
    "I01 real hierarchy exact acquisition short refusal and interior V-cycle preserve owners") {
    const auto assertions_before = assertion_counter().load();
    hierarchy_exact_case();
    hierarchy_short_case();
    hierarchy_interior_case();
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
