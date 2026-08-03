#pragma once

#include <Windows.h>

namespace gc::font {

using CreateFontIndirectWApi = HFONT(WINAPI*)(const LOGFONTW*);

namespace detail {

[[nodiscard]] HFONT InvokeCreateFontIndirectWDetour(
    const LOGFONTW* requested,
    CreateFontIndirectWApi original) noexcept;

} // namespace detail

[[nodiscard]] bool InstallJapaneseFontCharsetCompatibility() noexcept;

} // namespace gc::font
