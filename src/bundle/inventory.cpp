// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/inventory.hpp"

#include "docenhance/bundle/record.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <new>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace docenhance::bundle {
using Json = nlohmann::json;
namespace {
// A path longer than this is refused: bundle paths are short by construction, and a bound here
// is one less thing a hostile record can grow.
constexpr std::size_t max_path_length = 128;

[[nodiscard]] core::Error rejected(std::string detail) {
    return {.code = core::ErrorCode::input, .message = "The run record " + std::move(detail)};
}

// Nesting depth, counted over the bytes themselves. A recursive-descent parser answers a deeply
// nested document with the stack rather than with an error, so the bound is applied first.
[[nodiscard]] bool within_depth(std::span<const std::byte> bytes, std::size_t limit) noexcept {
    std::size_t depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (const auto raw : bytes) {
        const auto character = static_cast<char>(raw);
        if (quoted) {
            quoted = escaped || character != '"';
            escaped = !escaped && character == '\\';
            continue;
        }
        if (character == '"') {
            quoted = true;
        } else if (character == '{' || character == '[') {
            ++depth;
            if (depth > limit) {
                return false;
            }
        } else if ((character == '}' || character == ']') && depth > 0) {
            --depth;
        }
    }
    return !quoted && depth == 0;
}

[[nodiscard]] bool hexadecimal(std::string_view text) noexcept {
    return text.size() == core::sha256_hex_length && std::ranges::all_of(text, [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

// Inside the bundle and nowhere else: relative, forward slashes, no parent step, no root, no
// backslash that a different platform would read as a separator.
[[nodiscard]] bool contained(std::string_view path) {
    if (path.empty() || path.size() > max_path_length || path.front() == '/' ||
        path.contains('\\') || path.contains(':')) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto stop = std::min(path.find('/', start), path.size());
        const auto part = path.substr(start, stop - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = stop + 1;
    }
    return true;
}

// Found by walking the object rather than by a lookup that orders the keys. Ordering a stored
// name against a wanted one subtracts the two lengths and reads the difference as signed, which
// for a stored name that is shorter reverses its meaning; the sanitizers this is built under
// refuse that conversion, and a document can choose the lengths. Comparing lengths first and
// contents only when they agree asks the same question and never subtracts them.
[[nodiscard]] const Json* member(const Json& parent, std::string_view name) {
    if (!parent.is_object()) {
        return nullptr;
    }
    for (const auto& entry : parent.items()) {
        if (std::string_view{entry.key()} == name) {
            return &entry.value();
        }
    }
    return nullptr;
}

[[nodiscard]] core::Result<Artifact> artifact_of(const Json& value) {
    const auto* const path = member(value, "path");
    const auto* const digest = member(value, "sha256");
    const auto* const bytes = member(value, "bytes");
    if (path == nullptr || !path->is_string() || digest == nullptr || !digest->is_string() ||
        bytes == nullptr || !bytes->is_number_unsigned()) {
        return std::unexpected(rejected("declares an artifact without a path, digest and size"));
    }
    const auto spelling = path->get<std::string>();
    const auto sha256 = digest->get<std::string>();
    if (!contained(spelling)) {
        return std::unexpected(rejected("names a path outside the bundle: " + spelling));
    }
    if (!hexadecimal(sha256)) {
        return std::unexpected(rejected("declares a digest that is not a SHA-256"));
    }
    return Artifact{
        .name = spelling,
        .identity = {.sha256 = sha256, .bytes = bytes->get<std::uint64_t>()},
    };
}

// Every artifact a record declares, in the order the record lists them.
[[nodiscard]] core::Result<std::vector<Artifact>> inventory_of(const Json& document) {
    std::vector<Artifact> inventory;
    const auto* const output = member(document, "output");
    if (output == nullptr) {
        return std::unexpected(rejected("declares no output"));
    }
    auto artifact = artifact_of(*output);
    if (!artifact) {
        return std::unexpected(artifact.error());
    }
    inventory.push_back(std::move(*artifact));
    const auto* const protection = member(document, "protection");
    if (protection != nullptr && !protection->is_null()) {
        const auto* const stored = member(*protection, "stored");
        if (stored == nullptr) {
            return std::unexpected(rejected("declares protection without the mask it stored"));
        }
        auto mask = artifact_of(*stored);
        if (!mask) {
            return std::unexpected(mask.error());
        }
        inventory.push_back(std::move(*mask));
    }
    if (inventory.size() > record_max_artifacts) {
        return std::unexpected(rejected("declares more artifacts than a bundle may contain"));
    }
    return inventory;
}
} // namespace

namespace {
// The directories a set of declared paths implies, and nothing else: assets/ is permitted because
// assets/protect-mask.png is declared.
[[nodiscard]] bool implied_directory(const DeclaredBundle& declared, std::string_view name) {
    return std::ranges::any_of(declared.inventory, [name](const Artifact& artifact) {
        return artifact.name.size() > name.size() && artifact.name.starts_with(name) &&
               artifact.name.at(name.size()) == '/';
    });
}
} // namespace

core::Result<void> agrees(const DeclaredBundle& declared,
                          std::span<const core::NamedContent> present,
                          std::span<const std::string> directories) {
    for (const auto& artifact : declared.inventory) {
        const auto found = std::ranges::find(present, artifact.name, &core::NamedContent::name);
        if (found == present.end()) {
            return std::unexpected(rejected("declares " + artifact.name + ", which is not here"));
        }
        if (found->identity.bytes != artifact.identity.bytes) {
            return std::unexpected(rejected("holds a " + artifact.name + " of another size"));
        }
        if (found->identity.sha256 != artifact.identity.sha256) {
            return std::unexpected(
                rejected("holds a " + artifact.name + " that is not the one recorded"));
        }
    }
    for (const auto& file : present) {
        const bool declared_here =
            file.name == record_name ||
            std::ranges::any_of(declared.inventory, [&file](const Artifact& artifact) {
                return artifact.name == file.name;
            });
        if (!declared_here) {
            return std::unexpected(rejected("holds " + file.name + ", which it does not declare"));
        }
    }
    for (const auto& directory : directories) {
        if (!implied_directory(declared, directory)) {
            return std::unexpected(
                rejected("holds a " + directory + " directory that no declared file needs"));
        }
    }
    return {};
}

core::Result<DeclaredBundle> read_record(std::span<const std::byte> bytes) {
    if (bytes.size() > record_max_bytes) {
        return std::unexpected(rejected("is larger than a run record may be"));
    }
    if (!within_depth(bytes, record_max_depth)) {
        return std::unexpected(rejected("is nested more deeply than a run record may be"));
    }
    try {
        // char is the narrow-character view of the same immutable bytes; the parser reads a
        // range of characters and the span carries the length it may read.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        // Parsed without exceptions: a malformed document comes back discarded, not thrown.
        const auto document = Json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            return std::unexpected(rejected("is not well-formed JSON"));
        }
        const auto* const header = member(document, "record");
        const auto* const version = header == nullptr ? nullptr : member(*header, "version");
        const auto* const run = header == nullptr ? nullptr : member(*header, "run");
        const auto* const recorded = header == nullptr ? nullptr : member(*header, "recorded");
        if (version == nullptr || !version->is_number_unsigned() || run == nullptr ||
            !run->is_string() || recorded == nullptr || !recorded->is_string()) {
            return std::unexpected(rejected("has no identifying header"));
        }
        if (version->get<unsigned>() != record_version) {
            return std::unexpected(
                rejected("was written in a version this build does not support"));
        }
        auto inventory = inventory_of(document);
        if (!inventory) {
            return std::unexpected(inventory.error());
        }
        return DeclaredBundle{
            .version = version->get<unsigned>(),
            .run = run->get<std::string>(),
            .recorded = recorded->get<std::string>(),
            .inventory = std::move(*inventory),
        };
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource, "Reading the run record exhausted memory");
    }
}
} // namespace docenhance::bundle
