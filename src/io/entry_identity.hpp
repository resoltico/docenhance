// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
namespace docenhance::io {
// Native identity compares named objects with retained ownership leases, including publication.
// It is not a content digest, persistent identifier or defense against arbitrary tampering.
struct EntryIdentity {
    std::uint64_t volume{};
    std::uint64_t object{};
    std::uint64_t object_high{};
    bool operator==(const EntryIdentity&) const = default;
};
[[nodiscard]] std::optional<EntryIdentity> entry_identity(const std::filesystem::path& path);
[[nodiscard]] std::optional<EntryIdentity> stream_identity(std::FILE* file) noexcept;
#ifdef _WIN32
[[nodiscard]] std::optional<EntryIdentity> handle_identity(void* handle) noexcept;
#else
[[nodiscard]] std::optional<EntryIdentity> descriptor_identity(int handle) noexcept;
#endif
// Keeps the native object alive so an observed identifier cannot be recycled during ownership
// checks. Windows leases request metadata access and allow normal rename/readback sharing.
class EntryLease {
  public:
    EntryLease() noexcept = default;
    EntryLease(const EntryLease&) = delete;
    EntryLease& operator=(const EntryLease&) = delete;
    EntryLease(EntryLease&& other) noexcept;
    EntryLease& operator=(EntryLease&& other) noexcept;
    ~EntryLease();
    [[nodiscard]] static EntryLease directory(const std::filesystem::path& path);
    [[nodiscard]] static EntryLease capture(std::FILE* file, const std::filesystem::path& path);
    [[nodiscard]] std::optional<EntryIdentity> identity() const noexcept;
    [[nodiscard]] bool matches(const std::filesystem::path& path) const;

  private:
    void close() noexcept;
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int handle_ = -1;
#endif
    EntryIdentity identity_{};
};
} // namespace docenhance::io
