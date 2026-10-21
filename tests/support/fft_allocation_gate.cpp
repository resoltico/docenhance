// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "fft_allocation_gate.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <thread>
namespace docenhance::tests::fft_allocation_gate {
std::atomic<unsigned> arrivals{0};
std::atomic<bool> released{true};
std::atomic<bool> failed{false};
namespace {
std::atomic<std::size_t> selected_bytes{0};
struct Worker {
    std::thread::id id;
    unsigned hits = 0;
};
std::array<Worker, worker_limit> workers{};
std::mutex mutex;
} // namespace
void arm(std::size_t bytes) noexcept {
    workers.fill({});
    arrivals.store(0);
    failed.store(false);
    released.store(false);
    selected_bytes.store(bytes);
}
void allocated(std::size_t bytes) noexcept {
    if (bytes != selected_bytes.load() || released.load()) {
        return;
    }
    bool hold = false;
    {
        const std::scoped_lock lock{mutex};
        const auto id = std::this_thread::get_id();
        bool found = false;
        for (auto& worker : workers) {
            if (worker.id == id || worker.id == std::thread::id{}) {
                worker.id = id;
                hold = ++worker.hits == 2;
                found = true;
                break;
            }
        }
        if (!found) {
            std::abort();
        }
    }
    if (hold) {
        arrivals.fetch_add(1);
        while (!released.load()) {
            std::this_thread::yield();
        }
    }
}
void finish() noexcept {
    if (!released.load()) {
        failed.store(true);
        released.store(true);
    }
}
void disable() noexcept {
    selected_bytes.store(0);
    released.store(true);
}
} // namespace docenhance::tests::fft_allocation_gate
