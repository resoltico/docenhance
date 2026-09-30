// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
namespace docenhance::io {
// Native object identity is used only to refuse cleanup when a known owned name was replaced.
// It is not a content digest, a persistent identifier or a defense against arbitrary tampering.
struct EntryIdentity {
    std::uint64_t volume{};
    std::uint64_t object{};
    bool operator==(const EntryIdentity&) const = default;
};
[[nodiscard]] std::optional<EntryIdentity> entry_identity(const std::filesystem::path& path);
[[nodiscard]] std::optional<EntryIdentity> stream_identity(std::FILE* file) noexcept;
} // namespace docenhance::io
