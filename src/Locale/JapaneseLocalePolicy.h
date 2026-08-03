#pragma once

#include <Windows.h>

namespace gc::locale_compatibility {

inline constexpr UINT kJapaneseCodePage = 932;
inline constexpr LCID kJapaneseLcid = 0x0411;

[[nodiscard]] UINT MapDefaultCodePage(UINT code_page) noexcept;
[[nodiscard]] TIME_ZONE_INFORMATION
TokyoTimeZoneInformation() noexcept;
[[nodiscard]] bool ConvertUtcToTokyo(
    const SYSTEMTIME& utc,
    SYSTEMTIME* local) noexcept;

} // namespace gc::locale_compatibility
