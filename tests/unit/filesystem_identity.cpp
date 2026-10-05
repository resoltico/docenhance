// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_native.hpp"
#include "bundle_stage.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/bundle_snapshot.hpp"
#include "docenhance/io/png.hpp"
#include "docenhance/io/publication.hpp"
#include "entry_identity.hpp"
#include "file_access.hpp"
#include "file_contents.hpp"
#include "native_publication.hpp"
#include "source_snapshot.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <system_error>
#ifndef _WIN32
#include <cstdio>
#ifdef __APPLE__
#include <sys/fcntl.h>
#else
#include <fcntl.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace docenhance::tests {
namespace {
class WorkingDirectory {
  public:
    explicit WorkingDirectory(const std::filesystem::path& directory)
        : original_(std::filesystem::current_path()) {
        std::filesystem::current_path(directory);
    }
    WorkingDirectory(const WorkingDirectory&) = delete;
    WorkingDirectory& operator=(const WorkingDirectory&) = delete;
    WorkingDirectory(WorkingDirectory&&) = delete;
    WorkingDirectory& operator=(WorkingDirectory&&) = delete;
    ~WorkingDirectory() {
        try {
            std::filesystem::current_path(original_);
        } catch (...) {
            std::abort();
        }
    }

  private:
    std::filesystem::path original_;
};
struct Writer {
    std::filesystem::path next_directory;
    bool replace = false;
    unsigned calls = 0;
};
core::Result<void> mutate_after_write(void* const state, const io::BundleSlot& slot) {
    auto& writer = *static_cast<Writer*>(state);
    ++writer.calls;
    const auto result = io::write_bytes(slot, "owned");
    if (!result) {
        return result;
    }
    if (writer.replace) {
        const auto stage = slot.path.parent_path();
        std::filesystem::rename(stage, stage.parent_path() / "retained");
        std::filesystem::create_directory(stage);
        std::ofstream{slot.path} << "owned"; // Same bytes are not the same native object.
    } else if (!writer.next_directory.empty()) {
        std::filesystem::current_path(writer.next_directory);
    }
    return {};
}
std::error_code copied_destination(const std::filesystem::path& /*from*/,
                                   const std::filesystem::path& to) noexcept {
    try {
        std::filesystem::create_directory(to);
        std::ofstream{to / "run.json"} << "owned";
    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
    return std::make_error_code(std::errc::io_error);
}
enum class MutationKind { replace_source, add_entry, replace_parent, replace_at_commit };
struct FilesystemMutation {
    std::filesystem::path original;
    std::filesystem::path retained;
    unsigned checkpoints = 0;
    MutationKind kind = MutationKind::replace_source;
};
FilesystemMutation& filesystem_mutation() {
    static FilesystemMutation state;
    return state;
}
core::Checkpoint mutation_checkpoint(MutationKind kind) {
    switch (kind) {
    case MutationKind::replace_parent:
        return core::Checkpoint::encode;
    case MutationKind::replace_at_commit:
        return core::Checkpoint::commit;
    case MutationKind::add_entry:
        return core::Checkpoint::verification;
    case MutationKind::replace_source:
        return core::Checkpoint::decode;
    }
    std::abort();
}
bool mutate_read(core::Checkpoint at) noexcept {
    auto& state = filesystem_mutation();
    const auto wanted = mutation_checkpoint(state.kind);
    const unsigned checkpoint =
        state.kind == MutationKind::replace_parent || state.kind == MutationKind::replace_at_commit
            ? 1
            : 2;
    if (at != wanted || ++state.checkpoints != checkpoint) {
        return false;
    }
    try {
        if (state.kind == MutationKind::add_entry) {
            std::ofstream{state.original / "foreign"} << "preserve";
        } else {
            std::filesystem::rename(state.original, state.retained);
            if (state.kind == MutationKind::replace_parent ||
                state.kind == MutationKind::replace_at_commit) {
                std::filesystem::create_directory(state.original);
            } else {
                std::ofstream{state.original} << "foreign";
            }
        }
    } catch (...) {
        std::abort();
    }
    return false;
}
} // namespace

TEST_CASE("Publication binds relative effects before a writer changes working directory",
          "[filesystem][publication]") {
    const TemporaryDirectory temporary{"docenhance-working-directory"};
    const auto initial = temporary.path / "initial";
    const auto other = temporary.path / "other";
    REQUIRE(std::filesystem::create_directory(initial));
    REQUIRE(std::filesystem::create_directory(other));
    REQUIRE(std::filesystem::create_directory(other / "result.staging-0"));
    std::ofstream{other / "result.staging-0/run.json"} << "foreign";
    const WorkingDirectory current{initial};
    Writer writer{.next_directory = other};
    const io::BundleFile file{
        .relative = "run.json",
        .state = &writer,
        .write = mutate_after_write,
    };
    const auto result = io::publish_bundle("result", std::span{&file, 1});
    REQUIRE(result);
#ifdef _WIN32
    CHECK(*result == "result\\run.json");
#else
    CHECK(*result == "result/run.json");
#endif
    CHECK(file_contents(initial / "result/run.json") == "owned");
    CHECK(file_contents(other / "result.staging-0/run.json") == "foreign");
    CHECK(!std::filesystem::exists(other / "result"));
}
TEST_CASE("A copied-content replacement stage cannot publish or run another writer",
          "[filesystem][publication]") {
    const TemporaryDirectory temporary{"docenhance-stage-object"};
    Writer first{.next_directory = {}, .replace = true};
    Writer second;
    const std::array files{
        io::BundleFile{.relative = "run.json", .state = &first, .write = mutate_after_write},
        io::BundleFile{.relative = "next", .state = &second, .write = mutate_after_write},
    };
    const auto result = io::publish_bundle(utf8_spelling(temporary.path / "result"), files);
    REQUIRE(!result);
    CHECK(result.error().publication == core::Publication::unknown);
    CHECK(first.calls == 1);
    CHECK(second.calls == 0);
    CHECK(!std::filesystem::exists(temporary.path / "result"));
    CHECK(file_contents(temporary.path / "retained/run.json") == "owned");
    CHECK(file_contents(temporary.path / "result.staging-0/run.json") == "owned");
}
TEST_CASE("Copied destination bytes do not establish ownership after an ambiguous rename",
          "[filesystem][publication]") {
    const TemporaryDirectory temporary{"docenhance-copied-destination"};
    Writer writer;
    const io::BundleFile file{
        .relative = "run.json",
        .state = &writer,
        .write = mutate_after_write,
    };
    const auto result = io::publish_bundle(utf8_spelling(temporary.path / "result"),
                                           std::span{&file, 1}, {}, copied_destination);
    REQUIRE(!result);
    CHECK(result.error().publication == core::Publication::unknown);
    CHECK(file_contents(temporary.path / "result/run.json") == "owned");
    CHECK(file_contents(temporary.path / "result.staging-0/run.json") == "owned");
}
TEST_CASE("Source acquisition keeps the originally opened object when its name is replaced",
          "[filesystem][source]") {
    const TemporaryDirectory temporary{"docenhance-source-object"};
    const auto path = temporary.path / "source";
    constexpr std::size_t bytes = std::size_t{2} * 64 * 1024;
    const std::string expected(bytes, 'x');
    std::ofstream{path, std::ios::binary} << expected;
    filesystem_mutation() = {.original = path, .retained = temporary.path / "retained"};
    core::Budget budget{bytes};
    const auto snapshot =
        io::read_source_snapshot(utf8_spelling(path), budget, core::Cancellation{{}, mutate_read});
    REQUIRE(snapshot);
    CHECK(std::ranges::equal(snapshot->bytes(), std::as_bytes(std::span{expected})));
    CHECK(file_contents(path) == "foreign");
    CHECK(file_contents(filesystem_mutation().retained) == expected);
}
TEST_CASE("Opened bundle readers preserve objects and refuse observed Windows ancestor changes",
          "[filesystem][bundle]") {
    const TemporaryDirectory temporary{"docenhance-bundle-ancestor"};
    const auto parent = temporary.path / "parent";
    const auto root = parent / "bundle";
    REQUIRE(std::filesystem::create_directories(root));
    std::ofstream{root / "run.json"} << "owned";
    const auto directory = io::BundleDirectory::open(root);
    REQUIRE(directory);
    const auto retained = temporary.path / "retained";
    std::error_code error;
    std::filesystem::rename(parent, retained, error);
    if (error) {
        CHECK(directory->bound());
        CHECK(directory->file("run.json"));
    } else {
        REQUIRE(std::filesystem::create_directories(root));
        std::ofstream{root / "run.json"} << "foreign";
        CHECK(!directory->bound());
#ifdef _WIN32
        CHECK(!directory->file("run.json"));
        CHECK(!directory->entries());
#else
        const auto file = directory->file("run.json");
        REQUIRE(file);
        std::array<char, 5> content{};
        REQUIRE(std::fread(content.data(), 1, content.size(), file->get()) == content.size());
        CHECK(std::string(content.data(), content.size()) == "owned");
#endif
    }
}
TEST_CASE("Bundle snapshot acquisition rejects inventory mutation while refunding buffers",
          "[filesystem][bundle]") {
    const TemporaryDirectory temporary{"docenhance-inventory-mutation"};
    const auto root = temporary.path / "bundle";
    REQUIRE(std::filesystem::create_directory(root));
    constexpr std::size_t record_bytes = std::size_t{2} * 64 * 1024;
    std::ofstream{root / "run.json"} << std::string(record_bytes, 'x');
    std::ofstream{root / "result.png"} << "owned";
    filesystem_mutation() = {.original = root, .retained = {}, .kind = MutationKind::add_entry};
    core::Budget budget{record_bytes * 2};
    const auto result = io::read_bundle(utf8_spelling(root), budget,
                                        core::Cancellation{{}, mutate_read}, record_bytes);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::input);
    CHECK(result.error().message.contains("inventory changed"));
    CHECK(budget.used() == 0);
    CHECK(file_contents(root / "foreign") == "preserve");
}
TEST_CASE("Codec creation rechecks the owned parent after its execution checkpoint callback",
          "[filesystem][publication]") {
    const TemporaryDirectory temporary{"docenhance-codec-parent"};
    io::Stage stage;
    REQUIRE(io::reserve_stage(stage, temporary.path / "result", {}));
    const auto slot = io::place(stage, "result.png");
    REQUIRE(slot);
    filesystem_mutation() = {
        .original = stage.directory,
        .retained = temporary.path / "retained",
        .kind = MutationKind::replace_parent,
    };
    core::Budget budget{4096};
    const auto image = image::Plane<std::uint8_t>::allocate(budget, 2, 2);
    REQUIRE(image);
    const auto result =
        io::write_verified_png(*slot, image->view(), budget, core::Cancellation{{}, mutate_read});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::output);
    CHECK(filesystem_mutation().checkpoints == 1);
    CHECK(empty_directory(stage.directory));
    CHECK(empty_directory(filesystem_mutation().retained));
    const auto abandoned = io::abandon(stage, result.error());
    CHECK(abandoned.publication == core::Publication::unknown);
    CHECK(std::filesystem::exists(stage.directory));
}
TEST_CASE("Native leases preserve original identities while allowing rename and readback",
          "[filesystem][ownership]") {
    const TemporaryDirectory temporary{"docenhance-object-lease"};
    const auto root = temporary.path / "root";
    REQUIRE(std::filesystem::create_directory(root));
    const auto directory = io::EntryLease::directory(root);
    REQUIRE(directory.identity());
    const auto path = root / "file";
    auto stream = io::open_for_writing(path);
    REQUIRE(stream);
    const auto file = io::EntryLease::capture(stream.get(), path);
    REQUIRE(file.identity());
    stream.reset();
    std::filesystem::rename(path, root / "retained-file");
    std::ofstream{path} << "foreign";
    CHECK(!file.matches(path));
    CHECK(file.matches(root / "retained-file"));
    CHECK(io::open_for_reading(root / "retained-file"));
    const auto retained = temporary.path / "retained-root";
    std::filesystem::rename(root, retained);
    REQUIRE(std::filesystem::create_directory(root));
    CHECK(!directory.matches(root));
    CHECK(directory.matches(retained));
}
TEST_CASE("A namespace change inside the commit checkpoint prevents the native rename",
          "[filesystem][publication]") {
    const TemporaryDirectory temporary{"docenhance-cutoff-identity"};
    const auto target = temporary.path / "result";
    filesystem_mutation() = {
        .original = temporary.path / "result.staging-0",
        .retained = temporary.path / "retained",
        .kind = MutationKind::replace_at_commit,
    };
    Writer writer;
    const io::BundleFile file{
        .relative = "run.json",
        .state = &writer,
        .write = mutate_after_write,
    };
    const auto result = io::publish_bundle(utf8_spelling(target), std::span{&file, 1},
                                           core::Cancellation{{}, mutate_read});
    REQUIRE(!result);
    CHECK(result.error().publication == core::Publication::unknown);
    CHECK(filesystem_mutation().checkpoints == 1);
    CHECK(!std::filesystem::exists(target));
    CHECK(empty_directory(filesystem_mutation().original));
    CHECK(file_contents(filesystem_mutation().retained / "run.json") == "owned");
}
#ifndef _WIN32
TEST_CASE("Native source opening rejects a FIFO with a live peer and validates regular handles",
          "[filesystem][source]") {
    const TemporaryDirectory temporary{"docenhance-regular-source"};
    const auto pipe = temporary.path / "pipe";
    constexpr unsigned int owner_read_write = 0600;
    REQUIRE(mkfifo(pipe.c_str(), owner_read_write) == 0);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const auto peer = ::open(pipe.c_str(), O_RDWR | O_NONBLOCK);
    REQUIRE(peer >= 0);
    const auto refused = io::open_for_reading(pipe);
    CHECK(!refused);
    CHECK(::close(peer) == 0);
    const auto source = temporary.path / "source";
    std::ofstream{source} << "owned";
    std::filesystem::create_symlink(source, temporary.path / "link");
    CHECK(io::open_for_reading(temporary.path / "link"));
    CHECK(!io::open_for_reading(temporary.path));
}
#endif
} // namespace docenhance::tests
