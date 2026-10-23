// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "fft_allocation_trace.hpp"

#include "allocation_observer.hpp"
#include "fft_trace_storage.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#ifdef _WIN32
#include <errhandlingapi.h>
#include <winerror.h>
#endif
namespace docenhance::tests::fft_allocation_trace {
namespace {
struct Event {
    std::uint64_t bytes;
    std::uint64_t kind;
};
static_assert(sizeof(Event) == 2 * sizeof(std::uint64_t));
static_assert(std::has_unique_object_representations_v<Event>);
static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
constexpr std::size_t event_capacity = 8192;
constexpr unsigned maximum_callers = 7;
std::array<Event, event_capacity> events{};
std::size_t tail = 0, prefix = 0, logical_limit = event_capacity;
std::size_t spill_limit = 0;
bool active = false, valid = true, ready = true;
storage::File file;
std::atomic_flag committing{};
class CommitLock {
  public:
    CommitLock() noexcept {
        while (committing.test_and_set(std::memory_order_acquire)) {
        }
    }
    ~CommitLock() {
        committing.clear(std::memory_order_release);
    }
    CommitLock(const CommitLock&) = delete;
    CommitLock& operator=(const CommitLock&) = delete;
    CommitLock(CommitLock&&) = delete;
    CommitLock& operator=(CommitLock&&) = delete;
};
bool quiescent() noexcept {
    return !allocation_observer::observing.load() && allocation_observer::live.load() == 0;
}
bool reset_epoch() noexcept {
    if (!quiescent() || !valid || (active && !file.reset())) {
        return false;
    }
    tail = prefix = 0;
    ready = true;
    return true;
}
bool begin_epoch(unsigned callers) noexcept {
    if (!quiescent() || active || !valid || callers == 0 || callers > maximum_callers ||
        !file.open(storage::Access::read_write)) {
        return false;
    }
    active = true;
    logical_limit = callers * event_capacity;
    spill_limit = (callers - 1) * event_capacity * sizeof(Event);
    return reset_epoch();
}
bool end_epoch() noexcept {
    if (!quiescent()) {
        return false;
    }
    const bool closed = file.close();
    active = false;
    ready = false;
    tail = prefix = spill_limit = 0;
    logical_limit = event_capacity;
    valid = closed;
    return closed;
}
bool commit(Event event) noexcept {
    const CommitLock lock;
    if (!valid || !ready || event.kind > 2 || prefix + tail >= logical_limit) {
        valid = false;
        return false;
    }
    if (tail == events.size()) {
        if (!active || !file.append_bytes(std::as_bytes(std::span{events}), spill_limit)) {
            valid = false;
            return false;
        }
        prefix += tail;
        tail = 0;
    }
    std::span{events}.subspan(tail, 1).front() = event;
    ++tail;
    return true;
}
bool replay_prefix() noexcept {
    return valid && ready && (!active || file.rewind_for_replay(prefix * sizeof(Event)));
}
bool emit(std::ostream& stream) {
    if (!quiescent() || !replay_prefix()) {
        return false;
    }
    stream << "[";
    for (std::size_t index = 0; index < prefix + tail; ++index) {
        Event event{};
        if (index < prefix) {
            if (!file.read_exact(std::as_writable_bytes(std::span{&event, 1}))) {
                return false;
            }
        } else {
            event = std::span{events}.subspan(index - prefix, 1).front();
        }
        if (event.kind > 2) {
            return false;
        }
        if (index != 0) {
            stream << ",";
        }
        stream << "[" << event.bytes << "," << event.kind << "]";
    }
    stream << "]";
    return stream.good();
}
Kind golden_kind(std::size_t index) noexcept {
    switch (index % 3) {
    case 0:
        return Kind::acquired;
    case 1:
        return Kind::released;
    default:
        return Kind::refused;
    }
}
Event golden(std::size_t index) noexcept {
    return {
        .bytes = static_cast<std::uint64_t>(index) + 17,
        .kind = static_cast<std::uint64_t>(golden_kind(index)),
    };
}
bool golden_anchors() noexcept {
    return golden(8191).bytes == 8208 && golden(8192).bytes == 8209 &&
           golden(16383).bytes == 16400 && golden(16384).bytes == 16401 &&
           golden(49151).bytes == 49168 && golden(49152).bytes == 49169 &&
           golden(57343).bytes == 57360;
}
bool observed_feed(std::size_t count) noexcept {
    const storage::Errors entering;
    allocation_observer::allocation_attempts.store(0);
    allocation_observer::c_allocation_attempts.store(0);
    allocation_observer::peak.store(0);
    errno = EDOM;
#ifdef _WIN32
    SetLastError(ERROR_INVALID_DATA);
#endif
    allocation_observer::observing.store(true);
    for (std::size_t index = 0; index < count; ++index) {
        const auto event = golden(index);
        record(static_cast<std::size_t>(event.bytes), golden_kind(index));
    }
    allocation_observer::observing.store(false);
    const storage::Errors observed;
    const bool preserved_errno = observed.saved_errno == EDOM;
#ifdef _WIN32
    const bool preserved_windows = observed.saved_windows == ERROR_INVALID_DATA;
#else
    constexpr bool preserved_windows = true;
#endif
    return preserved_errno && preserved_windows &&
           allocation_observer::allocation_attempts.load() == 0 &&
           allocation_observer::c_allocation_attempts.load() == 0 &&
           allocation_observer::peak.load() == 0 && allocation_observer::live.load() == 0 &&
           allocation_observer::observation_valid();
}
bool golden_replay(std::size_t count) {
    std::ostringstream actual;
    if (!emit(actual)) {
        return false;
    }
    constexpr std::array suffixes{",0]", ",1]", ",2]"};
    std::string expected = "[";
    std::uint64_t next_byte = 17;
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) {
            expected += ',';
        }
        expected += '[';
        expected += std::to_string(next_byte++);
        expected += suffixes.at(index % suffixes.size());
    }
    expected += ']';
    return actual.str() == expected;
}
bool golden_case(unsigned callers, std::size_t count) {
    const SpillSession session{callers};
    if (!observed_feed(count) || prefix + tail != count || !golden_replay(count) ||
        file.bytes_written() != prefix * sizeof(Event)) {
        return false;
    }
    // A second epoch must exclude stale spill bytes and retain the same one-array boundary.
    return reset_epoch() && observed_feed(event_capacity) && prefix == 0 &&
           file.bytes_written() == 0 && golden_replay(event_capacity);
}
bool cap_refusal(unsigned callers) {
    if (callers == 0 ? (active || !reset_epoch()) : !begin_epoch(callers)) {
        return false;
    }
    const bool fed = observed_feed(logical_limit);
    const bool refused = !commit(golden(0));
    std::ostringstream rejected;
    const bool sticky = !reset_epoch() && !emit(rejected);
    const bool closed = end_epoch();
    const bool closed_replay = !emit(rejected);
    return fed && refused && sticky && rejected.str().empty() && closed && closed_replay;
}
bool invalid_refusal() {
    if (active || !reset_epoch()) {
        return false;
    }
    const bool refused = !commit({.bytes = 17, .kind = 3});
    const bool sticky = !reset_epoch();
    const bool closed = end_epoch();
    return refused && sticky && closed;
}
bool file_refusals() {
    storage::File negative;
    std::byte byte{0x5a};
    const auto bytes = std::span{&byte, 1};
    if (!negative.open(storage::Access::read_only)) {
        return false;
    }
    const bool denied = !negative.append_bytes(bytes, 1) && !negative.healthy();
    const bool poisoned = !negative.reset() && !negative.rewind_for_replay(0);
    if (!negative.close() || !denied || !poisoned || !negative.open(storage::Access::read_write)) {
        return false;
    }
    const bool eof = negative.rewind_for_replay(0) && !negative.read_exact(bytes) &&
                     !negative.healthy() && !negative.rewind_for_replay(0);
    const bool closed = negative.close();
    const bool reopened = negative.open(storage::Access::read_write);
    const bool fresh = reopened && negative.healthy() && negative.bytes_written() == 0;
    const bool mismatch = fresh && negative.append_bytes(bytes, 1) &&
                          !negative.rewind_for_replay(0) && !negative.healthy();
    return eof && closed && fresh && mismatch && negative.close();
}
} // namespace
SpillSession::SpillSession(unsigned callers) {
    if (!begin_epoch(callers)) {
        std::abort();
    }
}
SpillSession::~SpillSession() {
    const bool healthy = valid && file.healthy();
    if (!end_epoch() || !healthy) {
        std::abort();
    }
}
void reset() noexcept {
    if (!reset_epoch()) {
        std::abort();
    }
}
void record(std::size_t bytes, Kind kind) noexcept {
    bool committed = false;
    {
        const storage::Errors saved;
        committed = commit(
            {.bytes = static_cast<std::uint64_t>(bytes), .kind = static_cast<std::uint64_t>(kind)});
    }
    if (!committed) {
        std::abort();
    }
}
void write(std::ostream& stream) {
    if (!emit(stream)) {
        std::abort();
    }
}
bool check_storage_contract() {
    if (!quiescent() || active || !reset_epoch()) {
        return false;
    }
    const bool healthy = golden_anchors() && golden_case(1, 8192) && golden_case(2, 8193) &&
                         golden_case(2, 16384) && golden_case(7, 57344);
    const bool negative =
        healthy && cap_refusal(0) && cap_refusal(7) && invalid_refusal() && file_refusals();
    return negative && reset_epoch();
}
} // namespace docenhance::tests::fft_allocation_trace
