#include "Locale/JapaneseLocaleCompatibility.h"

#include "Locale/JapaneseLocalePolicy.h"

#include <plog/Log.h>

#include <array>
#include <memory>
#include <span>
#include <utility>

namespace gc::locale_compatibility {
namespace {

OriginalJapaneseLocaleApi g_originals{};
std::atomic_bool g_set_local_time_notification{};

UINT WINAPI GetACPDetour() noexcept {
    return kJapaneseCodePage;
}

UINT WINAPI GetOEMCPDetour() noexcept {
    return kJapaneseCodePage;
}

LCID WINAPI GetThreadLocaleDetour() noexcept {
    return kJapaneseLcid;
}

LCID WINAPI GetUserDefaultLCIDDetour() noexcept {
    return kJapaneseLcid;
}

BOOL WINAPI GetCPInfoDetour(
    UINT code_page,
    LPCPINFO info) noexcept {
    return detail::InvokeGetCPInfo(
        code_page,
        info,
        g_originals.get_cp_info);
}

int WINAPI MultiByteToWideCharDetour(
    UINT code_page,
    DWORD flags,
    LPCCH source,
    int source_size,
    LPWSTR destination,
    int destination_size) noexcept {
    return detail::InvokeMultiByteToWideChar(
        code_page,
        flags,
        source,
        source_size,
        destination,
        destination_size,
        g_originals.multi_byte_to_wide_char);
}

int WINAPI WideCharToMultiByteDetour(
    UINT code_page,
    DWORD flags,
    LPCWCH source,
    int source_size,
    LPSTR destination,
    int destination_size,
    LPCCH default_character,
    LPBOOL used_default_character) noexcept {
    return detail::InvokeWideCharToMultiByte(
        code_page,
        flags,
        source,
        source_size,
        destination,
        destination_size,
        default_character,
        used_default_character,
        g_originals.wide_char_to_multi_byte);
}

DWORD WINAPI GetTimeZoneInformationDetour(
    LPTIME_ZONE_INFORMATION information) noexcept {
    return detail::InvokeGetTimeZoneInformation(information);
}

void WINAPI GetLocalTimeDetour(
    LPSYSTEMTIME local) noexcept {
    detail::InvokeGetLocalTime(
        local,
        ::GetSystemTime,
        g_originals.get_local_time);
}

void LogSetLocalTimeSuppression() noexcept {
    try {
        PLOG_INFO
            << "JapaneseLocaleCompatibility: SetLocalTime suppressed"
            << " host_clock_unchanged=true";
    } catch (...) {
    }
}

BOOL WINAPI SetLocalTimeDetour(
    const SYSTEMTIME*) noexcept {
    return detail::SuppressSetLocalTime(
        g_set_local_time_notification,
        &LogSetLocalTimeSuppression);
}

} // namespace

BOOL detail::InvokeGetCPInfo(
    UINT code_page,
    LPCPINFO info,
    GetCPInfoApi original) noexcept {
    return original != nullptr
        ? original(MapDefaultCodePage(code_page), info)
        : FALSE;
}

int detail::InvokeMultiByteToWideChar(
    UINT code_page,
    DWORD flags,
    LPCCH source,
    int source_size,
    LPWSTR destination,
    int destination_size,
    MultiByteToWideCharApi original) noexcept {
    return original != nullptr
        ? original(
              MapDefaultCodePage(code_page),
              flags,
              source,
              source_size,
              destination,
              destination_size)
        : 0;
}

int detail::InvokeWideCharToMultiByte(
    UINT code_page,
    DWORD flags,
    LPCWCH source,
    int source_size,
    LPSTR destination,
    int destination_size,
    LPCCH default_character,
    LPBOOL used_default_character,
    WideCharToMultiByteApi original) noexcept {
    return original != nullptr
        ? original(
              MapDefaultCodePage(code_page),
              flags,
              source,
              source_size,
              destination,
              destination_size,
              default_character,
              used_default_character)
        : 0;
}

DWORD detail::InvokeGetTimeZoneInformation(
    LPTIME_ZONE_INFORMATION information) noexcept {
    const auto incoming_error = GetLastError();
    if (information == nullptr) {
        SetLastError(incoming_error);
        return TIME_ZONE_ID_INVALID;
    }
    *information = TokyoTimeZoneInformation();
    SetLastError(incoming_error);
    return TIME_ZONE_ID_UNKNOWN;
}

void detail::InvokeGetLocalTime(
    LPSYSTEMTIME local,
    GetSystemTimeApi get_system_time,
    GetLocalTimeApi fallback) noexcept {
    const auto incoming_error = GetLastError();
    if (local != nullptr && get_system_time != nullptr) {
        SYSTEMTIME utc{};
        get_system_time(&utc);
        SYSTEMTIME converted{};
        if (ConvertUtcToTokyo(utc, &converted)) {
            *local = converted;
            SetLastError(incoming_error);
            return;
        }
    }
    if (fallback != nullptr) {
        fallback(local);
    }
    SetLastError(incoming_error);
}

BOOL detail::SuppressSetLocalTime(
    std::atomic_bool& notification_latch,
    SetLocalTimeObserver observer) noexcept {
    const auto incoming_error = GetLastError();
    if (!notification_latch.exchange(
            true,
            std::memory_order_acq_rel) &&
        observer != nullptr) {
        observer();
    }
    SetLastError(incoming_error);
    return TRUE;
}

std::expected<void, hooking::HookError> AddJapaneseLocaleHooks(
    hooking::HookPlan& plan, gc::nesys_service::ProcessRole) noexcept {
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "GetACP"}, {L"kernel32.dll", "GetACP"},
            &GetACPDetour, &g_originals.get_acp); !added) return added;
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "GetOEMCP"}, {L"kernel32.dll", "GetOEMCP"},
            &GetOEMCPDetour, &g_originals.get_oem_cp); !added) return added;
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "GetThreadLocale"}, {L"kernel32.dll", "GetThreadLocale"},
            &GetThreadLocaleDetour, &g_originals.get_thread_locale); !added) return added;
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "GetUserDefaultLCID"}, {L"kernel32.dll", "GetUserDefaultLCID"},
            &GetUserDefaultLCIDDetour, &g_originals.get_user_default_lcid); !added) return added;
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "GetCPInfo"}, {L"kernel32.dll", "GetCPInfo"},
            &GetCPInfoDetour, &g_originals.get_cp_info); !added) return added;
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "MultiByteToWideChar"}, {L"kernel32.dll", "MultiByteToWideChar"},
            &MultiByteToWideCharDetour, &g_originals.multi_byte_to_wide_char); !added) return added;
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "WideCharToMultiByte"}, {L"kernel32.dll", "WideCharToMultiByte"},
            &WideCharToMultiByteDetour, &g_originals.wide_char_to_multi_byte); !added) return added;
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "GetTimeZoneInformation"}, {L"kernel32.dll", "GetTimeZoneInformation"},
            &GetTimeZoneInformationDetour, &g_originals.get_time_zone_information); !added) return added;
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "GetLocalTime"}, {L"kernel32.dll", "GetLocalTime"},
            &GetLocalTimeDetour, &g_originals.get_local_time); !added) return added;
    if (const auto added = plan.AddInlineExport(
            {"JapaneseLocale", "SetLocalTime"}, {L"kernel32.dll", "SetLocalTime"},
            &SetLocalTimeDetour, &g_originals.set_local_time); !added) return added;
    return {};
}

} // namespace gc::locale_compatibility
