// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/illumination.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/working.hpp"
#include "illumination_reference.hpp"
#include "illumination_working.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"
#include "surface_detail.hpp"
#include "surface_working_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace {
constexpr std::uint32_t cell = 8, dimension_choices = 9;
constexpr std::size_t budget_limit = std::size_t{4} * 1024 * 1024;
constexpr double scale = 255, tolerance = 1e-6;
std::uint8_t byte_at(std::span<const std::uint8_t> bytes, std::size_t i) {
    return i < bytes.size() ? bytes.subspan(i, 1).front() : 0;
}
// Bright paper samples, with whole cells of dark content chosen by bytes after the pixel bytes.
void fill(docenhance::image::PlaneView<double> source,
          docenhance::image::PlaneView<std::uint8_t> mask, std::span<const std::uint8_t> bytes) {
    constexpr unsigned bright_floor = 128;
    constexpr unsigned bright_choices = 127;
    constexpr unsigned dark_floor = 3;
    constexpr unsigned dark_choices = 28;
    constexpr unsigned dark_period = 8;
    const auto width = source.width();
    const auto columns = (width + cell - 1) / cell;
    const auto cell_bytes = 2 + (std::size_t{width} * source.height());
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto v = byte_at(bytes, 2 + (std::size_t{y} * width) + x);
            const auto cell_index = (std::size_t{y / cell} * columns) + (x / cell);
            const bool dark = byte_at(bytes, cell_bytes + cell_index) % dark_period == 1;
            const double value = dark ? (dark_floor + (v % dark_choices)) / scale
                                      : (bright_floor + (v % bright_choices)) / scale;
            source.row(y).subspan(x, 1).front() = value;
            mask.row(y).subspan(x, 1).front() =
                static_cast<std::uint8_t>(v % cell == 0 && x % cell == 0);
        }
    }
}
void compare_surface_field(const docenhance::methods::SurfaceFit& fit,
                           const docenhance::tests::LogReference& reference,
                           docenhance::image::Extent extent) {
    namespace methods = docenhance::methods;
    using docenhance::fuzz::require;
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const auto background = methods::surface_background_value(
                fit.grid, fit.logarithms.view(), {.x = x, .y = y});
            require(std::isfinite(background) &&
                        std::abs(background - reference.background(x, y)) < tolerance,
                    "direct independent I01 dense field");
        }
    }
}
void compare(docenhance::image::PlaneView<double> source,
             docenhance::image::PlaneView<const std::uint8_t> mask,
             docenhance::core::Budget& budget) {
    namespace methods = docenhance::methods;
    using docenhance::fuzz::require;
    const auto method = methods::Surface::create({.cell = cell});
    require(method.has_value(), "bounded I01 parameters admit");
    const auto concurrency = docenhance::exec::Concurrency::resolve(1, 1, 0, 0);
    require(concurrency.has_value(), "bounded I01 concurrency admits");
    const docenhance::exec::Scheduler scheduler{*concurrency};
    const methods::Working working{source, mask, budget, scheduler};
    const auto counts = methods::eligibility(working);
    require(counts.has_value(), "bounded I01 eligibility admits");
    const auto grid =
        methods::surface_grid({.width = source.width(), .height = source.height()}, *method);
    require(grid.has_value(), "bounded I01 grid admits");
    const auto reference = docenhance::tests::illumination_reference(source.as_const(), mask, cell,
                                                                     method->parameters().smooth);
    const auto resident = budget.used();
    const std::vector<double> before(source.storage().begin(), source.storage().end());
    {
        const auto fit = methods::fit_surface(*method, working, {.counts = *counts}, *grid);
        constexpr double explicit_coverage = 0.25;
        const auto cells = static_cast<double>(grid->columns * grid->rows);
        if (reference.measured_cells == 0 || reference.measured_cells / cells < explicit_coverage) {
            require(!fit && fit.error().code == docenhance::core::ErrorCode::method_inapplicable,
                    "insufficient measured cells are refused");
        } else {
            require(fit.has_value(), "bounded valid I01 fit");
            require(fit->facts.dark_cells == reference.dark_cells &&
                        fit->facts.measured_cells == reference.measured_cells,
                    "independent I01 measured/dark cell counts");
            compare_surface_field(*fit, reference,
                                  {.width = source.width(), .height = source.height()});
            require(budget.used() > resident, "actual coefficient owner remains charged");
        }
        require(std::ranges::equal(before, source.storage()), "fitting never stores source Y");
    }
    require(budget.used() == resident, "actual fit owner refunds");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace core = docenhance::core;
    namespace image = docenhance::image;
    const std::span<const std::uint8_t> bytes{data, size};
    const image::Extent extent{
        .width = cell + (byte_at(bytes, 0) % dimension_choices),
        .height = cell + (byte_at(bytes, 1) % dimension_choices),
    };
    core::Budget budget{budget_limit};
    auto source = image::Plane<double>::allocate(budget, extent.width, extent.height);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height);
    docenhance::fuzz::require(source.has_value() && mask.has_value(),
                              "bounded I01 source owners allocate");
    std::ranges::fill(source->view().storage(), -1);
    std::ranges::fill(mask->view().storage(), 255);
    fill(source->view(), mask->view(), bytes);
    const auto held = budget.used();
    compare(source->view(), mask->view().as_const(), budget);
    docenhance::fuzz::illumination_working(source->view().as_const(), mask->view().as_const());
    docenhance::fuzz::require(budget.used() == held, "I01 fit scratch refunded");
    return 0;
}
