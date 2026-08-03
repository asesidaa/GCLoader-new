#pragma once

#include <array>
#include <cstddef>

#include <Windows.h>

namespace gc::font {

using CreateFontIndirectWApi = HFONT(WINAPI*)(const LOGFONTW*);
using AddFontResourceExAApi = int(WINAPI*)(LPCSTR, DWORD, PVOID);
using FontResourceObserver = void(*)(
    LPCSTR,
    DWORD,
    PVOID,
    int,
    DWORD) noexcept;

namespace detail {

[[nodiscard]] bool IsInfinityFontFace(
    const LOGFONTW* requested) noexcept;

[[nodiscard]] HFONT InvokeCreateFontIndirectWDetour(
    const LOGFONTW* requested,
    CreateFontIndirectWApi original) noexcept;

[[nodiscard]] int InvokeAddFontResourceExADetour(
    LPCSTR file,
    DWORD flags,
    PVOID reserved,
    AddFontResourceExAApi original,
    FontResourceObserver observer) noexcept;

[[nodiscard]] std::array<char, 48> FormatNtGdiEntryBytes(
    const std::array<std::byte, 16>& bytes) noexcept;

} // namespace detail

[[nodiscard]] bool InstallJapaneseFontCharsetCompatibility() noexcept;

} // namespace gc::font
