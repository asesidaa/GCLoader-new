#pragma once

#include <Windows.h>
#include <utility>

namespace gc::platform::win32 {

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE value) noexcept
        : value_(Normalize(value)) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) reset(other.release());
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }
    [[nodiscard]] HANDLE release() noexcept { return std::exchange(value_, nullptr); }
    void reset(HANDLE value = nullptr) noexcept
    {
        value = Normalize(value);
        if (value_ == value) return;
        const HANDLE previous = std::exchange(value_, value);
        if (previous)
        {
            const DWORD captured_error = GetLastError();
            CloseHandle(previous);
            SetLastError(captured_error);
        }
    }

private:
    [[nodiscard]] static HANDLE Normalize(HANDLE value) noexcept
    {
        return value == INVALID_HANDLE_VALUE ? nullptr : value;
    }
    HANDLE value_{};
};

} // namespace gc::platform::win32
