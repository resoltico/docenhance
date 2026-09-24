// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/binarization.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/fixed_threshold.hpp"
#include "docenhance/methods/sauvola.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <variant>

namespace docenhance::methods {
core::Result<FixedThreshold> FixedThreshold::create(double threshold) {
    if (!std::isfinite(threshold) || threshold < 0.0 || threshold > 1.0) {
        return core::failure(core::ErrorCode::argument, "B03 requires a finite threshold in [0,1]");
    }
    return FixedThreshold{threshold};
}
core::Result<Sauvola> Sauvola::create(SauvolaParameters parameters) {
    const auto [window, k, r] = parameters;
    if (window < min_window || window > max_window || window % 2 == 0) {
        return core::failure(core::ErrorCode::argument, "B02 requires an odd window in [3,4095]");
    }
    if (!std::isfinite(k) || k < 0.0 || k > 1.0) {
        return core::failure(core::ErrorCode::argument, "B02 requires finite k in [0,1]");
    }
    if (!std::isfinite(r) || r < min_r || r > 1.0) {
        return core::failure(core::ErrorCode::argument,
                             "B02 requires finite normalized R in [1/255,1]");
    }
    return Sauvola{parameters};
}
ImplementedMethod describe(const Binarization& method) {
    return std::visit([](const auto& parameters) { return parameters.descriptor(); }, method);
}
namespace {
struct Scratch {
    std::uint32_t width;
    unsigned workers;
    core::Result<std::size_t> operator()(const FixedThreshold& /*method*/) const {
        return 0;
    }
    core::Result<std::size_t> operator()(const Sauvola& method) const {
        const auto plan = sauvola_workspace(width, method, workers);
        return plan ? core::Result<std::size_t>{plan->bytes} : std::unexpected(plan.error());
    }
};
struct Apply {
    image::PlaneView<const std::uint8_t> source;
    image::PlaneView<std::uint8_t> destination;
    BinarizationContext context;
    core::Result<void> operator()(const FixedThreshold& method) const {
        return fixed_threshold(source, destination, method.threshold());
    }
    core::Result<void> operator()(const Sauvola& method) const {
        return sauvola(source, destination, method, context);
    }
};
} // namespace
core::Result<std::size_t> scratch_bytes(const Binarization& method, std::uint32_t width,
                                        unsigned workers) {
    return std::visit(Scratch{.width = width, .workers = workers}, method);
}
core::Result<void> binarize(image::PlaneView<const std::uint8_t> source,
                            image::PlaneView<std::uint8_t> destination, const Binarization& method,
                            BinarizationContext context) {
    return std::visit(Apply{.source = source, .destination = destination, .context = context},
                      method);
}
} // namespace docenhance::methods
