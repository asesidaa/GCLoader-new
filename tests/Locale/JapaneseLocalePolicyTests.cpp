#include "Locale/JapaneseLocalePolicy.h"

#include <Windows.h>

#include <iostream>
#include <string_view>

namespace {

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

bool SameSystemTime(
    const SYSTEMTIME& actual,
    const SYSTEMTIME& expected) noexcept {
    return actual.wYear == expected.wYear &&
        actual.wMonth == expected.wMonth &&
        actual.wDayOfWeek == expected.wDayOfWeek &&
        actual.wDay == expected.wDay &&
        actual.wHour == expected.wHour &&
        actual.wMinute == expected.wMinute &&
        actual.wSecond == expected.wSecond &&
        actual.wMilliseconds == expected.wMilliseconds;
}

int TestCodePageMapping() {
    using namespace gc::locale_compatibility;

    int failures = 0;
    failures += Expect(
        MapDefaultCodePage(CP_ACP) == 932,
        "CP_ACP maps to CP932");
    failures += Expect(
        MapDefaultCodePage(CP_THREAD_ACP) == 932,
        "CP_THREAD_ACP maps to CP932");
    failures += Expect(
        MapDefaultCodePage(CP_OEMCP) == CP_OEMCP &&
            MapDefaultCodePage(CP_UTF8) == CP_UTF8 &&
            MapDefaultCodePage(932) == 932,
        "explicit code pages pass through");
    failures += Expect(
        kJapaneseCodePage == 932 && kJapaneseLcid == 0x0411,
        "Japanese locale values are exact");
    return failures;
}

int TestTokyoTimeZone() {
    const auto zone =
        gc::locale_compatibility::TokyoTimeZoneInformation();

    int failures = 0;
    failures += Expect(
        zone.Bias == -540 &&
            zone.StandardBias == 0 &&
            zone.DaylightBias == 0,
        "Tokyo timezone is UTC plus nine");
    failures += Expect(
        zone.StandardDate.wYear == 0 &&
            zone.StandardDate.wMonth == 0 &&
            zone.StandardDate.wDayOfWeek == 0 &&
            zone.StandardDate.wDay == 0 &&
            zone.StandardDate.wHour == 0 &&
            zone.StandardDate.wMinute == 0 &&
            zone.StandardDate.wSecond == 0 &&
            zone.StandardDate.wMilliseconds == 0 &&
            zone.DaylightDate.wYear == 0 &&
            zone.DaylightDate.wMonth == 0 &&
            zone.DaylightDate.wDayOfWeek == 0 &&
            zone.DaylightDate.wDay == 0 &&
            zone.DaylightDate.wHour == 0 &&
            zone.DaylightDate.wMinute == 0 &&
            zone.DaylightDate.wSecond == 0 &&
            zone.DaylightDate.wMilliseconds == 0,
        "Tokyo timezone has no DST transitions");
    failures += Expect(
        std::wstring_view{zone.StandardName} ==
                L"Tokyo Standard Time" &&
            std::wstring_view{zone.DaylightName} ==
                L"Tokyo Standard Time",
        "Tokyo timezone names are stable");
    return failures;
}

int TestTokyoCalendarBoundaries() {
    using gc::locale_compatibility::ConvertUtcToTokyo;

    int failures = 0;

    SYSTEMTIME leap_local{};
    const SYSTEMTIME leap_utc{
        2024, 2, 0, 29, 16, 30, 0, 0};
    const SYSTEMTIME leap_expected{
        2024, 3, 5, 1, 1, 30, 0, 0};
    failures += Expect(
        ConvertUtcToTokyo(leap_utc, &leap_local) &&
            SameSystemTime(leap_local, leap_expected),
        "Tokyo conversion crosses leap-day month boundary");

    SYSTEMTIME year_local{};
    const SYSTEMTIME year_utc{
        2025, 12, 0, 31, 23, 59, 59, 999};
    const SYSTEMTIME year_expected{
        2026, 1, 4, 1, 8, 59, 59, 999};
    failures += Expect(
        ConvertUtcToTokyo(year_utc, &year_local) &&
            SameSystemTime(year_local, year_expected),
        "Tokyo conversion crosses year boundary");

    return failures;
}

int TestInvalidConversionPreservesOutput() {
    using gc::locale_compatibility::ConvertUtcToTokyo;

    const SYSTEMTIME invalid{
        2025, 2, 0, 30, 12, 0, 0, 0};
    const SYSTEMTIME canary{
        1999, 7, 2, 14, 3, 4, 5, 6};
    auto output = canary;

    int failures = 0;
    failures += Expect(
        !ConvertUtcToTokyo(invalid, &output) &&
            SameSystemTime(output, canary),
        "invalid UTC input preserves output");
    failures += Expect(
        !ConvertUtcToTokyo(invalid, nullptr),
        "null output is rejected");
    return failures;
}

} // namespace

int main() {
    const int failures =
        TestCodePageMapping() +
        TestTokyoTimeZone() +
        TestTokyoCalendarBoundaries() +
        TestInvalidConversionPreservesOutput();
    return failures == 0 ? 0 : 1;
}
