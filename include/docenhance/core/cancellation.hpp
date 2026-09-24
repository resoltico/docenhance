// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"

#include <stop_token>
#include <utility>

namespace docenhance::core {
// Execution boundaries, not method configuration or a promise of elapsed-time responsiveness.
enum class Checkpoint {
    admission,
    allocation,
    scheduling,
    initialization,
    processing,
    decode,
    encode,
    staging,
    commit,
};
class Cancellation {
  public:
    // A static-lifetime noexcept observation function. No captured or borrowed object pointer.
    // Production uses the process interrupt latch; tests can synchronize named checkpoints.
    using Probe = bool (*)(Checkpoint) noexcept;
    Cancellation() = default;
    explicit Cancellation(std::stop_token token, Probe probe = nullptr) noexcept
        : token_(std::move(token)), probe_(probe) {}
    [[nodiscard]] bool requested(Checkpoint at) const noexcept {
        return token_.stop_requested() || (probe_ != nullptr && probe_(at));
    }

  private:
    std::stop_token token_;
    Probe probe_ = nullptr;
};
[[nodiscard]] inline std::unexpected<Error> cancelled() {
    return failure(ErrorCode::cancelled, "Cancelled");
}
} // namespace docenhance::core
