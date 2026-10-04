// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/bundle.hpp"
#include "docenhance/io/png.hpp"
#include "png_fixture.hpp"
#include "png_rows.hpp"
#include "png_transaction.hpp"
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
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

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
    REQUIRE(publish_png_fixture(utf8_spelling(original), image->view().as_const(), source_budget));
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
            publish_png_fixture(utf8_spelling(output), image->view().as_const(), constrained);
        if (!result) {
            CHECK(result.error().code == core::ErrorCode::resource);
            CHECK(result.error().publication == core::Publication::not_published);
            CHECK(!std::filesystem::exists(output));
            CHECK(!std::filesystem::exists(utf8_spelling(output) + ".staging-0"));
        }
        CHECK(constrained.used() == 0);
    }
}

TEST_CASE("Binary output is read back and compared before it is published", "[io][png]") {
    const TemporaryDirectory temporary{"docenhance-binary-verify"};
    core::Budget budget{mebibyte};
    auto image = image::Plane<std::uint8_t>::allocate(budget, 8, 4);
    REQUIRE(image);
    for (std::uint32_t y = 0; y < image->height(); ++y) {
        std::ranges::fill(image->view().row(y), y % 2 == 0 ? 0 : UINT8_MAX);
    }
    const auto published = temporary.path / "verified";
    REQUIRE(publish_png_fixture(utf8_spelling(published), image->view().as_const(), budget));

    // What the verifier compares against, and what it does when the file disagrees with it. A
    // single flipped byte inside the compressed data must be a refusal, not a published result.
    const auto result = published / "result.png";
    std::string bytes;
    {
        std::ifstream reading{result, std::ios::binary};
        bytes.assign(std::istreambuf_iterator<char>{reading}, std::istreambuf_iterator<char>{});
    }
    REQUIRE(bytes.size() > 40);
    const auto tampered = temporary.path / "tampered.png";
    {
        std::string damaged = bytes;
        auto& byte = damaged.at(damaged.size() - 12);
        byte = static_cast<char>(static_cast<std::uint8_t>(byte) ^ 0xFFU);
        std::ofstream writing{tampered, std::ios::binary};
        writing.write(damaged.data(), static_cast<std::streamsize>(damaged.size()));
    }
    CHECK(io::verify_png_image(result, image->view().as_const(), budget));
    const auto refused = io::verify_png_image(tampered, image->view().as_const(), budget);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::output_verify);
}

namespace {
// A bundle file whose bytes are known, so a test can check what was published.
struct TextFile {
    std::string_view relative;
    std::string content;
    bool fail = false;
};
core::Result<void> write_text(void* const state, const io::BundleSlot& slot) {
    const auto& file = *static_cast<TextFile*>(state);
    if (file.fail) {
        return core::failure(core::ErrorCode::output, "This file refuses to be written");
    }
    return io::write_bytes(slot, file.content);
}
std::vector<io::BundleFile> declared(std::span<TextFile> files) {
    std::vector<io::BundleFile> bundle;
    for (auto& file : files) {
        bundle.push_back({.relative = file.relative, .state = &file, .write = write_text});
    }
    return bundle;
}
} // namespace

TEST_CASE("A bundle is published whole or not at all", "[io]") {
    const TemporaryDirectory temporary{"docenhance-bundle"};
    std::array<TextFile, 3> files{
        TextFile{.relative = "result.png", .content = "image"},
        TextFile{.relative = "assets/protect-mask.png", .content = "mask"},
        TextFile{.relative = "run.json", .content = "{}"},
    };

    SECTION("every declared file, including one in a directory the bundle owns") {
        const auto output = temporary.path / "complete";
        const auto published =
            io::publish_bundle(utf8_spelling(output), declared(files), {}, io::rename_exclusive);
        REQUIRE(published);
        CHECK(std::filesystem::exists(output / "result.png"));
        CHECK(std::filesystem::exists(output / "assets" / "protect-mask.png"));
        CHECK(std::filesystem::exists(output / "run.json"));
    }

    SECTION("a file that cannot be written publishes nothing and leaves nothing behind") {
        files.back().fail = true;
        const auto output = temporary.path / "refused";
        const auto published =
            io::publish_bundle(utf8_spelling(output), declared(files), {}, io::rename_exclusive);
        REQUIRE(!published);
        CHECK(published.error().publication == core::Publication::not_published);
        CHECK(!std::filesystem::exists(output));
        // Staging is removed with the directories it created, and nothing else is touched.
        std::size_t entries = 0;
        for ([[maybe_unused]] const auto& entry :
             std::filesystem::directory_iterator{temporary.path}) {
            ++entries;
        }
        CHECK(entries == 0);
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
                CHECK(from_file->image.width() == from_memory->width());
                CHECK(from_file->image.height() == from_memory->height());
                CHECK(
                    std::ranges::equal(from_file->image.view().row(0), from_memory->view().row(0)));
                CHECK(from_file->source.sha256.size() == core::sha256_hex_length);
                CHECK(from_file->source.bytes == bytes.size());
            }
        }
        CHECK(file_budget.used() == 0);
        CHECK(memory_budget.used() == 0);
    }
}

TEST_CASE("A recorded name keeps the bytes the path was admitted with", "[io]") {
    // A document's name is the one part of a bundle that carries the original's own characters,
    // and a platform that spells paths in wide characters must not hand them back through an
    // encoding that cannot express them.
    const std::string admitted = "dokuments-\u0101-\u6587.png";
    CHECK(io::file_name("folder/" + admitted) == admitted);
    CHECK(io::file_name(admitted) == admitted);
    // A path that names no file at all is reported as it was given, not as an empty name.
    CHECK(io::file_name("").empty());
}
} // namespace docenhance::tests
