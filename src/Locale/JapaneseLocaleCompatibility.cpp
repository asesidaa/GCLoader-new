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
std::unique_ptr<gc::win32_hooks::MinHookTransaction>
    g_transaction;
std::atomic_bool g_set_local_time_notification{};

template <typename Function>
LPVOID DetourAddress(Function function) noexcept {
    return reinterpret_cast<LPVOID>(function);
}

template <typename Function>
LPVOID* OriginalSlot(Function* function) noexcept {
    return reinterpret_cast<LPVOID*>(function);
}

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

void LogInstallSuccess(
    gc::nesys_service::ProcessRole role) noexcept {
    try {
        PLOG_INFO
            << "JapaneseLocaleCompatibility: hooks active"
            << " role="
            << gc::nesys_service::ProcessRoleName(role)
            << " acp=" << kJapaneseCodePage
            << " lcid=0x411"
            << " utc_offset_minutes=540";
    } catch (...) {
    }
}

gc::win32_hooks::HookInstallError AllocationError() noexcept {
    return {
        .stage = gc::win32_hooks::HookInstallStage::initialize,
        .win32_error = ERROR_NOT_ENOUGH_MEMORY,
        .minhook_status = MH_ERROR_MEMORY_ALLOC,
    };
}

gc::win32_hooks::HookInstallError UnexpectedError() noexcept {
    return {
        .stage = gc::win32_hooks::HookInstallStage::initialize,
        .win32_error = ERROR_UNHANDLED_EXCEPTION,
        .minhook_status = MH_UNKNOWN,
    };
}

} // namespace

JapaneseLocaleHookRequests BuildJapaneseLocaleHookRequests(
    OriginalJapaneseLocaleApi* originals) noexcept {
    auto slot = [originals]<typename Function>(
                    Function OriginalJapaneseLocaleApi::* member) noexcept
        -> LPVOID* {
        return originals != nullptr
            ? OriginalSlot(&(originals->*member))
            : nullptr;
    };

    return {{
        {
            L"kernel32.dll",
            "GetACP",
            DetourAddress(&GetACPDetour),
            slot(&OriginalJapaneseLocaleApi::get_acp),
        },
        {
            L"kernel32.dll",
            "GetOEMCP",
            DetourAddress(&GetOEMCPDetour),
            slot(&OriginalJapaneseLocaleApi::get_oem_cp),
        },
        {
            L"kernel32.dll",
            "GetThreadLocale",
            DetourAddress(&GetThreadLocaleDetour),
            slot(&OriginalJapaneseLocaleApi::get_thread_locale),
        },
        {
            L"kernel32.dll",
            "GetUserDefaultLCID",
            DetourAddress(&GetUserDefaultLCIDDetour),
            slot(&OriginalJapaneseLocaleApi::get_user_default_lcid),
        },
        {
            L"kernel32.dll",
            "GetCPInfo",
            DetourAddress(&GetCPInfoDetour),
            slot(&OriginalJapaneseLocaleApi::get_cp_info),
        },
        {
            L"kernel32.dll",
            "MultiByteToWideChar",
            DetourAddress(&MultiByteToWideCharDetour),
            slot(&OriginalJapaneseLocaleApi::multi_byte_to_wide_char),
        },
        {
            L"kernel32.dll",
            "WideCharToMultiByte",
            DetourAddress(&WideCharToMultiByteDetour),
            slot(&OriginalJapaneseLocaleApi::wide_char_to_multi_byte),
        },
        {
            L"kernel32.dll",
            "GetTimeZoneInformation",
            DetourAddress(&GetTimeZoneInformationDetour),
            slot(&OriginalJapaneseLocaleApi::get_time_zone_information),
        },
        {
            L"kernel32.dll",
            "GetLocalTime",
            DetourAddress(&GetLocalTimeDetour),
            slot(&OriginalJapaneseLocaleApi::get_local_time),
        },
        {
            L"kernel32.dll",
            "SetLocalTime",
            DetourAddress(&SetLocalTimeDetour),
            slot(&OriginalJapaneseLocaleApi::set_local_time),
        },
    }};
}

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

std::expected<void, gc::win32_hooks::HookInstallError>
InstallJapaneseLocaleCompatibility(
    gc::nesys_service::ProcessRole role) noexcept {
    if (g_transaction != nullptr) {
        return {};
    }

    try {
        auto candidate =
            std::make_unique<gc::win32_hooks::MinHookTransaction>();
        const auto requests =
            BuildJapaneseLocaleHookRequests(&g_originals);
        const auto installed = candidate->Install(
            std::span<const gc::win32_hooks::HookRequest>{requests});
        if (!installed) {
            g_originals = {};
            return std::unexpected(installed.error());
        }

        g_transaction = std::move(candidate);
        LogInstallSuccess(role);
        return {};
    } catch (const std::bad_alloc&) {
        g_originals = {};
        return std::unexpected(AllocationError());
    } catch (...) {
        g_originals = {};
        return std::unexpected(UnexpectedError());
    }
}

} // namespace gc::locale_compatibility
