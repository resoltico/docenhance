// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "publication.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

namespace docenhance::tests {
namespace {
constexpr std::size_t mebibyte = std::size_t{1024} * 1024;
class TemporaryDirectory {
  public:
    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path() /
               ("docenhance-io-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        REQUIRE(std::filesystem::create_directory(path));
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
    // NOLINTNEXTLINE(bugprone-exception-escape)
    ~TemporaryDirectory() noexcept {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};
std::string utf8_name(const std::filesystem::path& path) {
    std::string result;
    for (char8_t const byte : path.u8string()) {
        result.push_back(static_cast<char>(byte));
    }
    return result;
}
} // namespace

TEST_CASE("Codec allocations share the caller budget and refund on every path", "[io]") {
    TemporaryDirectory const temporary;
    core::Budget source_budget{mebibyte};
    auto image = image::Plane<std::uint8_t>::allocate(source_budget, 2, 1);
    REQUIRE(image);
    image->view().row(0).front() = 0;
    image->view().row(0).back() = UINT8_MAX;
    const auto charged = source_budget.used();
    const auto original = temporary.path / "original";
    REQUIRE(
        io::publish_grayscale_png(utf8_name(original), image->view().as_const(), source_budget));
    CHECK(source_budget.used() == charged);
    constexpr auto limits = std::to_array<std::size_t>({0, 64, 1024, 4096, 65536, mebibyte});
    for (const auto limit : limits) {
        core::Budget constrained{limit};
        const auto output = temporary.path / ("budget-" + std::to_string(limit));
        {
            const auto decoded =
                io::load_grayscale_png(utf8_name(original / "result.png"), constrained);
            if (!decoded) {
                CHECK(decoded.error().code == core::ErrorCode::resource);
            }
            if (limit == 0) {
                CHECK(!decoded);
            }
        }
        CHECK(constrained.used() == 0);
        const auto result =
            io::publish_grayscale_png(utf8_name(output), image->view().as_const(), constrained);
        if (!result) {
            CHECK(result.error().code == core::ErrorCode::resource);
            CHECK(result.error().publication == core::Publication::not_published);
            CHECK(!std::filesystem::exists(output));
            CHECK(!std::filesystem::exists(utf8_name(output) + ".staging-0"));
        }
        CHECK(constrained.used() == 0);
    }
}

TEST_CASE("The native commit refuses even an existing empty directory", "[io]") {
    TemporaryDirectory const temporary;
    const auto source = temporary.path / "source";
    const auto target = temporary.path / "target";
    REQUIRE(std::filesystem::create_directory(source));
    REQUIRE(std::filesystem::create_directory(target));
    const auto error = io::rename_exclusive(source, target);
    CHECK(error);
    CHECK(io::definitely_not_published(error));
    CHECK(std::filesystem::is_directory(source));
    CHECK(std::filesystem::is_directory(target));
}

TEST_CASE("Only one concurrent native commit can win", "[io]") {
    TemporaryDirectory const temporary;
    const auto first = temporary.path / "first";
    const auto second = temporary.path / "second";
    const auto target = temporary.path / "target";
    REQUIRE(std::filesystem::create_directory(first));
    REQUIRE(std::filesystem::create_directory(second));
    std::atomic<unsigned> ready = 0;
    std::atomic<bool> start = false;
    std::error_code first_error;
    std::error_code second_error;
    {
        std::jthread const a{[&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            first_error = io::rename_exclusive(first, target);
        }};
        std::jthread const b{[&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            second_error = io::rename_exclusive(second, target);
        }};
        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        start.store(true, std::memory_order_release);
    }
    CHECK((first_error.value() == 0) != (second_error.value() == 0));
    CHECK(std::filesystem::is_directory(target));
    CHECK(std::filesystem::exists(first) != std::filesystem::exists(second));
}
} // namespace docenhance::tests
