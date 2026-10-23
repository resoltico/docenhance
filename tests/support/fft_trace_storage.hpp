// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
namespace docenhance::tests::fft_allocation_trace::storage {
enum class Access { read_write, read_only };
// Shared by the trace callback and File: capture Windows state before touching CRT errno.
struct Errors {
    Errors() noexcept = default;
    Errors(const Errors&) = delete;
    Errors& operator=(const Errors&) = delete;
    Errors(Errors&&) = delete;
    Errors& operator=(Errors&&) = delete;
#ifdef _WIN32
    DWORD saved_windows = GetLastError();
#endif
    int saved_errno = errno;
    ~Errors() {
        errno = saved_errno;
#ifdef _WIN32
        SetLastError(saved_windows);
#endif
    }
};
// The caller opens/resets/closes outside observation, and serializes all I/O through commit.
class File {
  public:
    File() noexcept = default;
    ~File() {
        if (!close()) {
            std::abort();
        }
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) = delete;
    File& operator=(File&&) = delete;
    [[nodiscard]] bool healthy() const noexcept {
        return valid_ && opened();
    }
    [[nodiscard]] std::size_t bytes_written() const noexcept {
        return written_;
    }
    [[nodiscard]] bool open(Access access = Access::read_write) noexcept {
        const Errors saved;
        if (opened() || close_failed_) {
            return poison();
        }
        valid_ = false;
        written_ = 0;
        replay_ = false;
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            std::array<char, 96> name{};
            if (!basename(name)) {
                return false;
            }
            if (create(name, access)) {
                valid_ = true;
                return true;
            }
#ifdef _WIN32
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) {
                return false;
            }
#else
            if (errno != EEXIST) {
                return false;
            }
#endif
        }
        return false;
    }
    // Any short transfer poisons the epoch; confirmed partial writes are counted, never retried.
    [[nodiscard]] bool append_bytes(std::span<const std::byte> bytes,
                                    std::size_t maximum_bytes) noexcept {
        const Errors saved;
        if (!healthy() || replay_ || written_ > maximum_bytes ||
            bytes.size() > maximum_bytes - written_ || !transfer_size(bytes.size())) {
            return poison();
        }
        if (bytes.empty()) {
            return true;
        }
#ifdef _WIN32
        DWORD count = 0;
        const bool success = WriteFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()),
                                       &count, nullptr) != 0;
        if (!success || count > bytes.size()) {
            return poison();
        }
        written_ += count;
        return count == bytes.size() || poison();
#else
        const auto count = ::write(handle_, bytes.data(), bytes.size());
        if (count <= 0 || static_cast<std::size_t>(count) > bytes.size()) {
            return poison();
        }
        written_ += static_cast<std::size_t>(count);
        return static_cast<std::size_t>(count) == bytes.size() || poison();
#endif
    }
    [[nodiscard]] bool rewind_for_replay(std::size_t expected_bytes) noexcept {
        const Errors saved;
        if (!healthy() || expected_bytes != written_ || !size_matches(expected_bytes) ||
            !rewind()) {
            return poison();
        }
        replay_ = true;
        return true;
    }
    // A nonempty read at EOF is an actual short-read refusal, not a synthetic size precheck.
    [[nodiscard]] bool read_exact(std::span<std::byte> bytes) noexcept {
        const Errors saved;
        if (!healthy() || !replay_ || !transfer_size(bytes.size())) {
            return poison();
        }
        if (bytes.empty()) {
            return true;
        }
#ifdef _WIN32
        DWORD count = 0;
        return (ReadFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &count,
                         nullptr) != 0 &&
                count == bytes.size()) ||
               poison();
#else
        const auto count = ::read(handle_, bytes.data(), bytes.size());
        return (count > 0 && static_cast<std::size_t>(count) == bytes.size()) || poison();
#endif
    }
    [[nodiscard]] bool reset() noexcept {
        const Errors saved;
        if (!healthy() || !rewind()) {
            return poison();
        }
#ifdef _WIN32
        if (SetEndOfFile(handle_) == 0) {
            return poison();
        }
#else
        if (::ftruncate(handle_, 0) != 0) {
            return poison();
        }
#endif
        written_ = 0;
        replay_ = false;
        return true;
    }
    [[nodiscard]] bool close() noexcept {
        const Errors saved;
        if (!opened()) {
            return !close_failed_;
        }
#ifdef _WIN32
        auto* const consumed = handle_;
#else
        const auto consumed = handle_;
#endif
        handle_ =
            invalid_handle(); // The effect is uncertain on failure: never retry this identity.
#ifdef _WIN32
        const bool success = CloseHandle(consumed) != 0;
#else
        const bool success = ::close(consumed) == 0;
#endif
        close_failed_ = !success;
        valid_ = valid_ && success;
        return success;
    }

  private:
    [[nodiscard]] bool poison() noexcept {
        valid_ = false;
        return false;
    }
    [[nodiscard]] bool opened() const noexcept {
        return handle_ != invalid_handle();
    }
    [[nodiscard]] static bool transfer_size(std::size_t size) noexcept {
#ifdef _WIN32
        return size <= std::numeric_limits<DWORD>::max();
#else
        return size <= static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());
#endif
    }
    [[nodiscard]] static bool basename(std::array<char, 96>& name) noexcept {
        static std::atomic<std::uint64_t> serial{0};
        auto number = serial.load();
        bool claimed = false;
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            if (number == std::numeric_limits<std::uint64_t>::max()) {
                return false;
            }
            if (serial.compare_exchange_weak(number, number + 1)) {
                claimed = true;
                break;
            }
        }
        if (!claimed) {
            return false;
        }
        constexpr std::string_view prefix{".docenhance-trace-"};
        std::span<char> room{name};
        if (prefix.size() >= room.size()) {
            return false;
        }
        std::ranges::copy(prefix, room.begin());
        room = room.subspan(prefix.size());
#ifdef _WIN32
        const auto process = GetCurrentProcessId();
#else
        const auto process = ::getpid();
#endif
        const auto first = std::to_chars(&room.front(), &room.back(), process);
        if (first.ec != std::errc{}) {
            return false;
        }
        const auto used = static_cast<std::size_t>(std::ranges::distance(room.data(), first.ptr));
        room = room.subspan(used);
        if (room.size() < 2) {
            return false;
        }
        room.front() = '-';
        room = room.subspan(1);
        const auto second = std::to_chars(&room.front(), &room.back(), number);
        return second.ec == std::errc{};
    }
    [[nodiscard]] bool create(const std::array<char, 96>& name, Access access) noexcept {
#ifdef _WIN32
        std::array<wchar_t, 96> wide{};
        for (std::size_t i = 0; i < name.size(); ++i) {
            std::span{wide}.subspan(i, 1).front() =
                static_cast<wchar_t>(std::span{name}.subspan(i, 1).front());
        }
        const DWORD permissions =
            static_cast<DWORD>(GENERIC_READ) | static_cast<DWORD>(DELETE) |
            (access == Access::read_write ? static_cast<DWORD>(GENERIC_WRITE) : DWORD{0});
        handle_ = CreateFileW(wide.data(), permissions, 0, nullptr, CREATE_NEW,
                              static_cast<DWORD>(FILE_ATTRIBUTE_TEMPORARY) |
                                  static_cast<DWORD>(FILE_FLAG_DELETE_ON_CLOSE),
                              nullptr);
        return opened();
#else
        constexpr unsigned creation =
            static_cast<unsigned>(O_CREAT) | static_cast<unsigned>(O_EXCL) |
            static_cast<unsigned>(O_CLOEXEC) | static_cast<unsigned>(O_NOFOLLOW);
        constexpr auto read_write = static_cast<unsigned>(O_RDWR);
        constexpr auto read_only = static_cast<unsigned>(O_RDONLY);
        static_assert((creation | read_write | read_only) <=
                      static_cast<unsigned>(std::numeric_limits<int>::max()));
        const unsigned flags = creation | (access == Access::read_write ? read_write : read_only);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        handle_ = ::open(name.data(), static_cast<int>(flags), 0600);
        if (!opened()) {
            return false;
        }
        if (::unlink(name.data()) != 0) {
            const int failure = errno;
            const bool closed = close();
            errno = failure;
            if (!closed) {
                std::abort();
            }
            return false;
        }
        return true;
#endif
    }
    [[nodiscard]] bool rewind() const noexcept {
#ifdef _WIN32
        const LARGE_INTEGER zero{};
        return SetFilePointerEx(handle_, zero, nullptr, FILE_BEGIN) != 0;
#else
        return ::lseek(handle_, 0, SEEK_SET) == 0;
#endif
    }
    [[nodiscard]] bool size_matches(std::size_t expected) const noexcept {
#ifdef _WIN32
        LARGE_INTEGER size{};
        return GetFileSizeEx(handle_, &size) != 0 && size.QuadPart >= 0 &&
               std::cmp_equal(size.QuadPart, expected);
#else
        struct stat size{};
        return ::fstat(handle_, &size) == 0 && size.st_size >= 0 &&
               std::cmp_equal(size.st_size, expected);
#endif
    }
#ifdef _WIN32
    [[nodiscard]] static HANDLE invalid_handle() noexcept {
        return INVALID_HANDLE_VALUE;
    }
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    [[nodiscard]] static int invalid_handle() noexcept {
        return -1;
    }
    int handle_ = -1;
#endif
    std::size_t written_ = 0;
    bool valid_ = false;
    bool replay_ = false;
    bool close_failed_ = false;
};
} // namespace docenhance::tests::fft_allocation_trace::storage
