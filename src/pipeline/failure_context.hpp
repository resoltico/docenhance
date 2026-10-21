// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <type_traits>
#include <utility>

namespace docenhance::pipeline {
template <typename Slot, typename Facts> void store_completed(Slot& slot, Facts facts) noexcept {
    // Every retained Facts alternative is a no-throw owned scalar value. Optional emplacement
    // only destroys/constructs that value; it cannot allocate or run a user callback.
    static_assert(std::is_nothrow_constructible_v<typename Slot::value_type, Facts>);
    static_assert(std::is_nothrow_destructible_v<typename Slot::value_type>);
    slot.emplace(std::move(facts));
}
} // namespace docenhance::pipeline
