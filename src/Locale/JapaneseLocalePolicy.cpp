#include "Locale/JapaneseLocalePolicy.h"

#include <algorithm>
#include <limits>
#include <string_view>

namespace gc::locale_compatibility {
namespace {

constexpr ULONGLONG kTokyoOffset100Nanoseconds =
    9ULL * 60ULL * 60ULL * 10'000'000ULL;
constexpr std::wstring_view kTokyoTimeZoneName =
    L"Tokyo Standard Time";

template <std::size_t Size>
void AssignTimeZoneName(
    wchar_t (&destination)[Size]) noexcept {
    static_assert(Size > kTokyoTimeZoneName.size());
    std::copy(
        kTokyoTimeZoneName.begin(),
        kTokyoTimeZoneName.end(),
        destination);
    destination[kTokyoTimeZoneName.size()] = L'\0';
}

} // namespace

UINT MapDefaultCodePage(UINT code_page) noexcept {
    return code_page == CP_ACP || code_page == CP_THREAD_ACP
        ? kJapaneseCodePage
        : code_page;
}

TIME_ZONE_INFORMATION TokyoTimeZoneInformation() noexcept {
    TIME_ZONE_INFORMATION zone{};
    zone.Bias = -540;
    zone.StandardBias = 0;
    zone.DaylightBias = 0;
    AssignTimeZoneName(zone.StandardName);
    AssignTimeZoneName(zone.DaylightName);
    return zone;
}

bool ConvertUtcToTokyo(
    const SYSTEMTIME& utc,
    SYSTEMTIME* local) noexcept {
    if (local == nullptr) {
        return false;
    }

    FILETIME utc_file_time{};
    if (SystemTimeToFileTime(&utc, &utc_file_time) == FALSE) {
        return false;
    }

    ULARGE_INTEGER ticks{};
    ticks.LowPart = utc_file_time.dwLowDateTime;
    ticks.HighPart = utc_file_time.dwHighDateTime;
    if (ticks.QuadPart >
        std::numeric_limits<ULONGLONG>::max() -
            kTokyoOffset100Nanoseconds) {
        return false;
    }
    ticks.QuadPart += kTokyoOffset100Nanoseconds;

    const FILETIME local_file_time{
        .dwLowDateTime = ticks.LowPart,
        .dwHighDateTime = ticks.HighPart,
    };
    SYSTEMTIME converted{};
    if (FileTimeToSystemTime(
            &local_file_time,
            &converted) == FALSE) {
        return false;
    }

    *local = converted;
    return true;
}

} // namespace gc::locale_compatibility
