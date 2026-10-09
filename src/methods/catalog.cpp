// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/catalog.hpp"

#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/geometry.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/reviewed_methods.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
namespace docenhance::methods {
namespace {
template <typename Variant, std::size_t First, std::size_t... Indices>
constexpr auto executable_variants(std::index_sequence<Indices...> /*indices*/) noexcept {
    return std::array{std::variant_alternative_t<First + Indices, Variant>::descriptor()...};
}
template <typename Variant, std::size_t First = 1> constexpr auto executable_variants() noexcept {
    return executable_variants<Variant, First>(
        std::make_index_sequence<std::variant_size_v<Variant> - First>{});
}
constexpr auto catalog = std::apply(
    [](const auto&... descriptors) { return std::array{descriptors...}; },
    std::tuple_cat(executable_variants<Illumination>(), executable_variants<Denoising>(),
                   executable_variants<Contrast>(), executable_variants<Sharpening>(),
                   executable_variants<Restoration>(), executable_variants<Binarization, 0>(),
                   std::tuple{ExactQuarterTurn::descriptor()}));
static_assert(std::ranges::equal(catalog, reviewed_methods),
              "Reviewed capabilities and executable method types must agree");
} // namespace
std::span<const ImplementedMethod> implemented_methods() noexcept {
    return catalog;
}
} // namespace docenhance::methods
