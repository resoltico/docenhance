// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>
#include <iosfwd>
namespace docenhance::tests::fft_allocation_trace {
enum class Kind { acquired, released, refused };
// One serial owner preopens storage before observations and closes it after joined replay.
class SpillSession {
  public:
    explicit SpillSession(unsigned callers);
    ~SpillSession();
    SpillSession(const SpillSession&) = delete;
    SpillSession& operator=(const SpillSession&) = delete;
    SpillSession(SpillSession&&) = delete;
    SpillSession& operator=(SpillSession&&) = delete;
};
void reset() noexcept;
void record(std::size_t bytes, Kind kind) noexcept;
void write(std::ostream& stream);
// Synthetic storage/order controls; these do not establish native allocation coverage.
[[nodiscard]] bool check_storage_contract();
} // namespace docenhance::tests::fft_allocation_trace
