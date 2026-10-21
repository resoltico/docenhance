// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"
#include "docenhance/methods/parameters.hpp"

#include <cstdint>
#include <tuple>
#include <variant>
namespace docenhance::methods {
class Unsharp {
  public:
    [[nodiscard]] static core::Result<Unsharp> create(UnsharpParameters p = {});
    [[nodiscard]] UnsharpParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return unsharp_descriptor;
    }

  private:
    explicit Unsharp(UnsharpParameters p) : parameters_(p) {}
    UnsharpParameters parameters_;
};
struct SharpenOff {};
using Sharpening = std::variant<SharpenOff, Unsharp>;
struct ExcursionRange {
    double low = 0;
    double high = 0;
    bool operator==(const ExcursionRange&) const = default;
};
[[nodiscard]] constexpr bool validate(ExcursionRange range) noexcept {
    return core::in_range(range.low, -sharpen_amount_domain.maximum,
                          1 + sharpen_amount_domain.maximum) &&
           core::in_range(range.high, -sharpen_amount_domain.maximum,
                          1 + sharpen_amount_domain.maximum) &&
           range.low <= range.high;
}
// Finite Unit entering f and fitted blurred value, with an admitted method.
// Returns finite RAW excursions, which can lie outside Unit before clipping.
[[nodiscard]] double unsharp_candidate(double f, double blurred, const Unsharp& method) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::Fields<methods::ExcursionRange> {
    static constexpr auto domain = core::Range<double>{
        .minimum = -methods::sharpen_amount_domain.maximum,
        .maximum = 1 + methods::sharpen_amount_domain.maximum,
    };
    static constexpr auto list = std::tuple{
        core::field("low", &methods::ExcursionRange::low, domain),
        core::field("high", &methods::ExcursionRange::high, domain),
    };
};
} // namespace docenhance
