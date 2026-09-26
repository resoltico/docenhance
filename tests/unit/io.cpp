// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_fixture.hpp"
#include "publication.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <thread>

namespace docenhance::tests {
namespace {
constexpr std::size_t mebibyte = std::size_t{1024} * 1024;
} // namespace

TEST_CASE("Codec allocations share the caller budget and refund on every path", "[io]") {
    const TemporaryDirectory temporary{"docenhance-io"};
    core::Budget source_budget{mebibyte};
    auto image = image::Plane<std::uint8_t>::allocate(source_budget, 2, 1);
    REQUIRE(image);
    image->view().row(0).front() = 0;
    image->view().row(0).back() = UINT8_MAX;
    const auto charged = source_budget.used();
    const auto original = temporary.path / "original";
    REQUIRE(io::publish_grayscale_png(utf8_spelling(original), image->view().as_const(),
                                      source_budget));
    CHECK(source_budget.used() == charged);
    constexpr auto limits = std::to_array<std::size_t>({0, 64, 1024, 4096, 65536, mebibyte});
    for (const auto limit : limits) {
        core::Budget constrained{limit};
        const auto output = temporary.path / ("budget-" + std::to_string(limit));
        {
            const auto decoded =
                io::load_grayscale_png(utf8_spelling(original / "result.png"), constrained);
            if (!decoded) {
                CHECK(decoded.error().code == core::ErrorCode::resource);
            }
            if (limit == 0) {
                CHECK(!decoded);
            }
        }
        CHECK(constrained.used() == 0);
        const auto result =
            io::publish_grayscale_png(utf8_spelling(output), image->view().as_const(), constrained);
        if (!result) {
            CHECK(result.error().code == core::ErrorCode::resource);
            CHECK(result.error().publication == core::Publication::not_published);
            CHECK(!std::filesystem::exists(output));
            CHECK(!std::filesystem::exists(utf8_spelling(output) + ".staging-0"));
        }
        CHECK(constrained.used() == 0);
    }
}

TEST_CASE("The native commit refuses even an existing empty directory", "[io]") {
    const TemporaryDirectory temporary{"docenhance-io"};
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
    const TemporaryDirectory temporary{"docenhance-io"};
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
TEST_CASE("File and memory PNG sources use identical decoding and error policy", "[io][png]") {
    const TemporaryDirectory temporary{"docenhance-io"};
    const auto input = temporary.path / "parity.png";
    const GrayFixture fixture{
        .width = 2,
        .height = 1,
        .depth = 2,
        .interlaced = true,
        .samples = {0, 3},
    };
    auto bytes = make_gray_png(fixture);
    for (const bool corrupt : {false, true}) {
        if (corrupt) {
            bytes.at(29) ^= 1U;
        }
        {
            std::ofstream stream{input, std::ios::binary};
            for (const auto byte : bytes) {
                stream.put(static_cast<char>(byte));
            }
            stream.close();
            REQUIRE(stream);
        }
        core::Budget file_budget{mebibyte};
        core::Budget memory_budget{mebibyte};
        {
            const auto from_file = io::load_grayscale_png(utf8_spelling(input), file_budget);
            const auto from_memory = io::decode_grayscale_png(bytes, memory_budget);
            REQUIRE(from_file.has_value() == from_memory.has_value());
            if (corrupt) {
                REQUIRE(!from_file);
                CHECK(from_file.error().code == from_memory.error().code);
                CHECK(from_file.error().code == core::ErrorCode::input);
            } else {
                REQUIRE(from_file);
                CHECK(from_file->width() == from_memory->width());
                CHECK(from_file->height() == from_memory->height());
                CHECK(std::ranges::equal(from_file->view().row(0), from_memory->view().row(0)));
            }
        }
        CHECK(file_budget.used() == 0);
        CHECK(memory_budget.used() == 0);
    }
}
} // namespace docenhance::tests
