// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::image {
namespace {
class SortControl {
  public:
    explicit SortControl(core::Cancellation cancellation)
        : cancellation_(std::move(cancellation)) {}
    bool stop() {
        constexpr std::size_t interval = 1024;
        return visits_++ % interval == 0 && cancellation_.requested(core::Checkpoint::measurement);
    }

  private:
    core::Cancellation cancellation_;
    std::size_t visits_ = 0;
};
bool sift(std::span<double> values, std::size_t root, std::size_t count, SortControl& control) {
    while (root < count / 2) {
        if (control.stop()) {
            return false;
        }
        auto child = (2 * root) + 1;
        if (child + 1 < count &&
            values.subspan(child, 1).front() < values.subspan(child + 1, 1).front()) {
            ++child;
        }
        if (values.subspan(root, 1).front() >= values.subspan(child, 1).front()) {
            break;
        }
        std::swap(values.subspan(root, 1).front(), values.subspan(child, 1).front());
        root = child;
    }
    return true;
}
} // namespace
core::Result<std::size_t> nearest_rank_index(std::size_t count, double p) {
    if (count == 0 || !std::isfinite(p) || p < 0 || p > 1) {
        return core::failure(core::ErrorCode::argument, "Invalid percentile rank");
    }
    if (p == 0) {
        return 0;
    }
    if (p == 1) {
        return count - 1;
    }
    const auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(count)));
    return rank == 0 ? 0 : std::min(rank - 1, count - 1);
}
core::Result<void> sort_samples(std::span<double> values, const core::Cancellation& cancellation) {
    SortControl control{cancellation};
    for (const auto v : values) {
        if (control.stop()) {
            return core::cancelled();
        }
        if (!std::isfinite(v)) {
            return core::failure(core::ErrorCode::argument, "Nonfinite percentile sample");
        }
    }
    for (auto root = values.size() / 2; root > 0; --root) {
        if (control.stop() || !sift(values, root - 1, values.size(), control)) {
            return core::cancelled();
        }
    }
    for (auto count = values.size(); count > 1; --count) {
        if (control.stop()) {
            return core::cancelled();
        }
        std::swap(values.front(), values.subspan(count - 1, 1).front());
        if (!sift(values, 0, count - 1, control)) {
            return core::cancelled();
        }
    }
    return {};
}
core::Result<double> nearest_rank(std::span<double> values, double p,
                                  const core::Cancellation& cancellation) {
    auto index = nearest_rank_index(values.size(), p);
    if (!index) {
        return std::unexpected(index.error());
    }
    auto sorted = sort_samples(values, cancellation);
    if (!sorted) {
        return std::unexpected(sorted.error());
    }
    return values.subspan(*index, 1).front();
}
} // namespace docenhance::image
