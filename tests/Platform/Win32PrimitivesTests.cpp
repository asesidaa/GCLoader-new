#include "Platform/Win32/UniqueHandle.h"
#include "Platform/Win32/Utf.h"
#include "Platform/Win32/Win32Error.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

namespace {
int failures{};
void Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool IsOpen(HANDLE handle)
{
    DWORD flags{};
    return GetHandleInformation(handle, &flags) != FALSE;
}

void StrictUtfConversions()
{
    using namespace gc::platform::win32;
    for (const std::wstring_view wide : {L"", L"ASCII", L"\u65e5\u672c\u8a9e"})
    {
        const auto utf8 = WideToUtf8(wide);
        Expect(utf8.has_value(), "valid UTF-16 converts");
        if (!utf8) continue;
        const auto round_trip = Utf8ToWide(*utf8);
        Expect(round_trip && *round_trip == wide, "Unicode round trip preserves text");
    }
    const auto empty = Utf8ToWide("");
    Expect(empty && empty->empty(), "empty UTF-8 produces empty UTF-16");
    const auto ascii = Utf8ToWide("ASCII");
    Expect(ascii && *ascii == L"ASCII", "ASCII encoding is unchanged");
    const auto invalid_utf8 = Utf8ToWide("\xc0\xaf");
    Expect(!invalid_utf8 && invalid_utf8.error().win32_error != ERROR_SUCCESS &&
           invalid_utf8.error().direction == UtfDirection::utf8_to_utf16,
           "overlong UTF-8 is rejected with captured error");
    const wchar_t surrogate[]{static_cast<wchar_t>(0xd800)};
    const auto invalid_utf16 = WideToUtf8(std::wstring_view{surrogate, 1});
    Expect(!invalid_utf16 && invalid_utf16.error().win32_error != ERROR_SUCCESS &&
           invalid_utf16.error().direction == UtfDirection::utf16_to_utf8,
           "lone UTF-16 surrogate is rejected with captured error");
}

void OrdinaryHandleOwnership()
{
    using gc::platform::win32::UniqueHandle;
    Expect(!UniqueHandle{} && !UniqueHandle{INVALID_HANDLE_VALUE},
           "null and invalid handles are empty");
    UniqueHandle first{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    UniqueHandle second{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Expect(first && second, "real unnamed events are created");
    if (!first || !second) return;
    const HANDLE transferred = first.get();
    UniqueHandle moved{std::move(first)};
    Expect(!first && moved.get() == transferred && IsOpen(transferred),
           "move construction transfers ownership");
    const HANDLE replaced = second.get();
    second = std::move(moved);
    Expect(!moved && second.get() == transferred && IsOpen(transferred),
           "move assignment transfers ownership");
    Expect(!IsOpen(replaced), "move assignment closes previous handle");
    const HANDLE released = second.release();
    Expect(!second && IsOpen(released), "release leaves handle open");
    Expect(CloseHandle(released) != FALSE, "released event is explicitly closed");
    UniqueHandle reset{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Expect(static_cast<bool>(reset), "reset event is created");
    if (!reset) return;
    const HANDLE reset_value = reset.get();
    reset.reset(reset_value);
    Expect(IsOpen(reset_value), "reset to same handle retains ownership");
    reset.reset(INVALID_HANDLE_VALUE);
    Expect(!reset && !IsOpen(reset_value), "reset closes and normalizes invalid handle");
    HANDLE scoped{};
    {
        UniqueHandle owner{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
        Expect(static_cast<bool>(owner), "scope event is created");
        scoped = owner.get();
    }
    if (scoped) Expect(!IsOpen(scoped), "scope exit closes the owned handle");
}

void CapturedErrorFormatting()
{
    using gc::platform::win32::FormatWin32Error;
    SetLastError(ERROR_ACCESS_DENIED);
    const auto known = FormatWin32Error(ERROR_FILE_NOT_FOUND);
    Expect(known && !known->empty(), "captured known error has system text");
    if (known)
        Expect(known->back() != L'\r' && known->back() != L'\n',
               "terminal CR/LF is trimmed");
    constexpr DWORD unknown = 0xfefefefe;
    const auto result = FormatWin32Error(unknown);
    Expect(result ? !result->empty() :
           result.error().source_error == unknown &&
               result.error().format_error != ERROR_SUCCESS,
           "unknown code retains the exact source error if formatting fails");
}
} // namespace

int main()
{
    StrictUtfConversions();
    OrdinaryHandleOwnership();
    CapturedErrorFormatting();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
