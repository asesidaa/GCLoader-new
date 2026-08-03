#include "Locale/JapaneseLocaleCompatibility.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <expected>
#include <iostream>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>

namespace {

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

struct GetCpInfoCapture {
    int calls{};
    UINT code_page{};
    LPCPINFO info{};
};

GetCpInfoCapture* g_get_cp_info{};

BOOL WINAPI CaptureGetCPInfo(UINT code_page, LPCPINFO info) {
    ++g_get_cp_info->calls;
    g_get_cp_info->code_page = code_page;
    g_get_cp_info->info = info;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
}

struct MultiByteCapture {
    int calls{};
    UINT code_page{};
    DWORD flags{};
    LPCCH source{};
    int source_size{};
    LPWSTR destination{};
    int destination_size{};
};

MultiByteCapture* g_multi_byte{};

int WINAPI CaptureMultiByteToWideChar(
    UINT code_page,
    DWORD flags,
    LPCCH source,
    int source_size,
    LPWSTR destination,
    int destination_size) {
    ++g_multi_byte->calls;
    g_multi_byte->code_page = code_page;
    g_multi_byte->flags = flags;
    g_multi_byte->source = source;
    g_multi_byte->source_size = source_size;
    g_multi_byte->destination = destination;
    g_multi_byte->destination_size = destination_size;
    SetLastError(ERROR_NO_UNICODE_TRANSLATION);
    return 17;
}

struct WideCharCapture {
    int calls{};
    UINT code_page{};
    DWORD flags{};
    LPCWCH source{};
    int source_size{};
    LPSTR destination{};
    int destination_size{};
    LPCCH default_character{};
    LPBOOL used_default_character{};
};

WideCharCapture* g_wide_char{};

int WINAPI CaptureWideCharToMultiByte(
    UINT code_page,
    DWORD flags,
    LPCWCH source,
    int source_size,
    LPSTR destination,
    int destination_size,
    LPCCH default_character,
    LPBOOL used_default_character) {
    ++g_wide_char->calls;
    g_wide_char->code_page = code_page;
    g_wide_char->flags = flags;
    g_wide_char->source = source;
    g_wide_char->source_size = source_size;
    g_wide_char->destination = destination;
    g_wide_char->destination_size = destination_size;
    g_wide_char->default_character = default_character;
    g_wide_char->used_default_character = used_default_character;
    SetLastError(ERROR_INVALID_FLAGS);
    return 23;
}

SYSTEMTIME g_fake_utc{};
SYSTEMTIME g_fallback_local{};
int g_get_system_time_calls{};
int g_fallback_calls{};

void WINAPI CaptureGetSystemTime(LPSYSTEMTIME output) {
    ++g_get_system_time_calls;
    *output = g_fake_utc;
    SetLastError(ERROR_BAD_ENVIRONMENT);
}

void WINAPI CaptureFallbackGetLocalTime(LPSYSTEMTIME output) {
    ++g_fallback_calls;
    *output = g_fallback_local;
    SetLastError(ERROR_BAD_FORMAT);
}

int g_set_local_time_observations{};

void CaptureSetLocalTimeSuppression() noexcept {
    ++g_set_local_time_observations;
    SetLastError(ERROR_INVALID_DATA);
}

int TestHookRequestSurface() {
    using namespace gc::locale_compatibility;

    constexpr std::array<std::string_view, 10> expected_exports{
        "GetACP",
        "GetOEMCP",
        "GetThreadLocale",
        "GetUserDefaultLCID",
        "GetCPInfo",
        "MultiByteToWideChar",
        "WideCharToMultiByte",
        "GetTimeZoneInformation",
        "GetLocalTime",
        "SetLocalTime",
    };

    OriginalJapaneseLocaleApi originals{};
    const auto requests = BuildJapaneseLocaleHookRequests(&originals);

    int failures = 0;
    failures += Expect(
        requests.size() == expected_exports.size(),
        "required public locale hook count is exact");
    for (std::size_t index = 0; index < requests.size(); ++index) {
        failures += Expect(
            requests[index].module_name != nullptr &&
                std::wstring_view{requests[index].module_name} ==
                    L"kernel32.dll" &&
                requests[index].export_name != nullptr &&
                std::string_view{requests[index].export_name} ==
                    expected_exports[index] &&
                requests[index].detour != nullptr &&
                requests[index].original != nullptr,
            "locale hook request identity is exact");
    }
    failures += Expect(
        std::ranges::all_of(
            requests,
            [&requests](const auto& candidate) {
                return std::ranges::count_if(
                    requests,
                    [&candidate](const auto& other) {
                        return std::string_view{candidate.export_name} ==
                            std::string_view{other.export_name};
                    }) == 1;
            }),
        "locale hook exports are unique");
    failures += Expect(
        std::ranges::all_of(
            requests,
            [&requests](const auto& candidate) {
                return std::ranges::count(
                    requests | std::views::transform(
                        [](const auto& request) {
                            return request.original;
                        }),
                    candidate.original) == 1;
            }),
        "locale hook original slots are distinct");

    using InstallResult = decltype(
        InstallJapaneseLocaleCompatibility(
            gc::nesys_service::ProcessRole::Game));
    failures += Expect(
        (std::is_same_v<
            InstallResult,
            std::expected<
                void,
                gc::win32_hooks::HookInstallError>>),
        "locale installer returns structured hook error");
    return failures;
}

int TestGetCpInfoForwarding() {
    using gc::locale_compatibility::detail::InvokeGetCPInfo;

    CPINFO info{};
    GetCpInfoCapture capture{};
    g_get_cp_info = &capture;

    SetLastError(ERROR_SUCCESS);
    const auto default_result = InvokeGetCPInfo(
        CP_ACP,
        &info,
        CaptureGetCPInfo);
    int failures = 0;
    failures += Expect(
        default_result == FALSE &&
            capture.calls == 1 &&
            capture.code_page == 932 &&
            capture.info == &info &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "GetCPInfo maps only the default code-page token");

    const auto explicit_result = InvokeGetCPInfo(
        CP_UTF8,
        &info,
        CaptureGetCPInfo);
    failures += Expect(
        explicit_result == FALSE &&
            capture.calls == 2 &&
            capture.code_page == CP_UTF8 &&
            capture.info == &info,
        "GetCPInfo preserves explicit code page");
    return failures;
}

int TestMultiByteForwarding() {
    using gc::locale_compatibility::detail::InvokeMultiByteToWideChar;

    constexpr std::array<std::pair<UINT, UINT>, 5> cases{
        std::pair{UINT{CP_ACP}, UINT{932}},
        std::pair{UINT{CP_THREAD_ACP}, UINT{932}},
        std::pair{UINT{CP_OEMCP}, UINT{CP_OEMCP}},
        std::pair{UINT{CP_UTF8}, UINT{CP_UTF8}},
        std::pair{UINT{932}, UINT{932}},
    };
    constexpr char source[] = "source";
    wchar_t destination[12]{};

    int failures = 0;
    for (const auto [input, expected] : cases) {
        MultiByteCapture capture{};
        g_multi_byte = &capture;
        SetLastError(ERROR_SUCCESS);
        const auto result = InvokeMultiByteToWideChar(
            input,
            MB_ERR_INVALID_CHARS,
            source,
            5,
            destination,
            11,
            CaptureMultiByteToWideChar);
        failures += Expect(
            result == 17 &&
                capture.calls == 1 &&
                capture.code_page == expected &&
                capture.flags == MB_ERR_INVALID_CHARS &&
                capture.source == source &&
                capture.source_size == 5 &&
                capture.destination == destination &&
                capture.destination_size == 11 &&
                GetLastError() == ERROR_NO_UNICODE_TRANSLATION,
            "MultiByteToWideChar changes only default code page");
    }
    return failures;
}

int TestWideCharForwarding() {
    using gc::locale_compatibility::detail::InvokeWideCharToMultiByte;

    constexpr std::array<std::pair<UINT, UINT>, 5> cases{
        std::pair{UINT{CP_ACP}, UINT{932}},
        std::pair{UINT{CP_THREAD_ACP}, UINT{932}},
        std::pair{UINT{CP_OEMCP}, UINT{CP_OEMCP}},
        std::pair{UINT{CP_UTF8}, UINT{CP_UTF8}},
        std::pair{UINT{932}, UINT{932}},
    };
    constexpr wchar_t source[] = L"source";
    char destination[13]{};
    constexpr char default_character = '!';
    BOOL used_default = FALSE;

    int failures = 0;
    for (const auto [input, expected] : cases) {
        WideCharCapture capture{};
        g_wide_char = &capture;
        SetLastError(ERROR_SUCCESS);
        const auto result = InvokeWideCharToMultiByte(
            input,
            WC_NO_BEST_FIT_CHARS,
            source,
            6,
            destination,
            12,
            &default_character,
            &used_default,
            CaptureWideCharToMultiByte);
        failures += Expect(
            result == 23 &&
                capture.calls == 1 &&
                capture.code_page == expected &&
                capture.flags == WC_NO_BEST_FIT_CHARS &&
                capture.source == source &&
                capture.source_size == 6 &&
                capture.destination == destination &&
                capture.destination_size == 12 &&
                capture.default_character == &default_character &&
                capture.used_default_character == &used_default &&
                GetLastError() == ERROR_INVALID_FLAGS,
            "WideCharToMultiByte changes only default code page");
    }
    return failures;
}

int TestTokyoApiSeams() {
    using namespace gc::locale_compatibility;

    int failures = 0;

    TIME_ZONE_INFORMATION zone{};
    SetLastError(ERROR_ACCESS_DENIED);
    const auto zone_id =
        detail::InvokeGetTimeZoneInformation(&zone);
    failures += Expect(
        zone_id == TIME_ZONE_ID_UNKNOWN &&
            zone.Bias == -540 &&
            zone.StandardDate.wMonth == 0 &&
            zone.DaylightDate.wMonth == 0 &&
            GetLastError() == ERROR_ACCESS_DENIED,
        "timezone hook returns fixed Tokyo and preserves last error");

    g_fake_utc = SYSTEMTIME{
        2025, 12, 0, 31, 23, 0, 0, 0};
    g_fallback_local = SYSTEMTIME{
        1988, 4, 5, 6, 7, 8, 9, 10};
    g_get_system_time_calls = 0;
    g_fallback_calls = 0;
    SYSTEMTIME local{};
    SetLastError(ERROR_INVALID_DATA);
    detail::InvokeGetLocalTime(
        &local,
        CaptureGetSystemTime,
        CaptureFallbackGetLocalTime);
    failures += Expect(
        local.wYear == 2026 &&
            local.wMonth == 1 &&
            local.wDay == 1 &&
            local.wHour == 8 &&
            g_get_system_time_calls == 1 &&
            g_fallback_calls == 0 &&
            GetLastError() == ERROR_INVALID_DATA,
        "GetLocalTime derives Tokyo from UTC");

    g_fake_utc = SYSTEMTIME{
        2025, 2, 0, 30, 12, 0, 0, 0};
    SetLastError(ERROR_NOT_READY);
    detail::InvokeGetLocalTime(
        &local,
        CaptureGetSystemTime,
        CaptureFallbackGetLocalTime);
    failures += Expect(
        local.wYear == g_fallback_local.wYear &&
            local.wMonth == g_fallback_local.wMonth &&
            local.wDay == g_fallback_local.wDay &&
            local.wHour == g_fallback_local.wHour &&
            g_get_system_time_calls == 2 &&
            g_fallback_calls == 1 &&
            GetLastError() == ERROR_NOT_READY,
        "GetLocalTime falls back without returning uninitialized output");

    return failures;
}

int TestSetLocalTimeSuppression() {
    using gc::locale_compatibility::detail::SuppressSetLocalTime;

    std::atomic_bool notification_latch{false};
    g_set_local_time_observations = 0;

    SetLastError(ERROR_ACCESS_DENIED);
    const auto first = SuppressSetLocalTime(
        notification_latch,
        CaptureSetLocalTimeSuppression);
    const auto first_error = GetLastError();
    SetLastError(ERROR_NOT_READY);
    const auto second = SuppressSetLocalTime(
        notification_latch,
        CaptureSetLocalTimeSuppression);
    const auto second_error = GetLastError();

    return Expect(
        first == TRUE && second == TRUE &&
            g_set_local_time_observations == 1 &&
            first_error == ERROR_ACCESS_DENIED &&
            second_error == ERROR_NOT_READY,
        "SetLocalTime is suppressed and reported once");
}

} // namespace

int main() {
    const int failures =
        TestHookRequestSurface() +
        TestGetCpInfoForwarding() +
        TestMultiByteForwarding() +
        TestWideCharForwarding() +
        TestTokyoApiSeams() +
        TestSetLocalTimeSuppression();
    return failures == 0 ? 0 : 1;
}
