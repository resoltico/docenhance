// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/catalog.hpp"

#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/method_catalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <variant>
namespace docenhance::methods {
namespace {
template <std::size_t... Indices>
constexpr auto executable_catalog(std::index_sequence<Indices...> /*indices*/) noexcept {
    return std::array{Surface::descriptor(),
                      std::variant_alternative_t<Indices, Binarization>::descriptor()...};
}
constexpr auto catalog =
    executable_catalog(std::make_index_sequence<std::variant_size_v<Binarization>>{});
static_assert(std::ranges::equal(catalog, reviewed_methods),
              "Reviewed capabilities and executable method types must agree");
} // namespace
std::span<const ImplementedMethod> implemented_methods() noexcept {
    return catalog;
}
} // namespace docenhance::methods
