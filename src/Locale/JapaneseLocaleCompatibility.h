#pragma once

#include "Nesys/NesysServiceProcess.h"
#include "Platform/Win32/Hooking/HookPlan.h"

#include <Windows.h>

#include <array>
#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <expected>

namespace gc::locale_compatibility {

using GetCPInfoApi = BOOL(WINAPI*)(UINT, LPCPINFO);
using MultiByteToWideCharApi = int(WINAPI*)(
    UINT, DWORD, LPCCH, int, LPWSTR, int);
using WideCharToMultiByteApi = int(WINAPI*)(
    UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);
using GetSystemTimeApi = void(WINAPI*)(LPSYSTEMTIME);
using GetLocalTimeApi = void(WINAPI*)(LPSYSTEMTIME);
using SetLocalTimeObserver = void(*)() noexcept;

struct OriginalJapaneseLocaleApi {
    decltype(&::GetACP) get_acp{};
    decltype(&::GetOEMCP) get_oem_cp{};
    decltype(&::GetThreadLocale) get_thread_locale{};
    decltype(&::GetUserDefaultLCID) get_user_default_lcid{};
    GetCPInfoApi get_cp_info{};
    MultiByteToWideCharApi multi_byte_to_wide_char{};
    WideCharToMultiByteApi wide_char_to_multi_byte{};
    decltype(&::GetTimeZoneInformation) get_time_zone_information{};
    GetLocalTimeApi get_local_time{};
    decltype(&::SetLocalTime) set_local_time{};
};

namespace detail {

[[nodiscard]] BOOL InvokeGetCPInfo(
    UINT code_page,
    LPCPINFO info,
    GetCPInfoApi original) noexcept;
[[nodiscard]] int InvokeMultiByteToWideChar(
    UINT code_page,
    DWORD flags,
    LPCCH source,
    int source_size,
    LPWSTR destination,
    int destination_size,
    MultiByteToWideCharApi original) noexcept;
[[nodiscard]] int InvokeWideCharToMultiByte(
    UINT code_page,
    DWORD flags,
    LPCWCH source,
    int source_size,
    LPSTR destination,
    int destination_size,
    LPCCH default_character,
    LPBOOL used_default_character,
    WideCharToMultiByteApi original) noexcept;
[[nodiscard]] DWORD InvokeGetTimeZoneInformation(
    LPTIME_ZONE_INFORMATION information) noexcept;
void InvokeGetLocalTime(
    LPSYSTEMTIME local,
    GetSystemTimeApi get_system_time,
    GetLocalTimeApi fallback) noexcept;
[[nodiscard]] BOOL SuppressSetLocalTime(
    std::atomic_bool& notification_latch,
    SetLocalTimeObserver observer) noexcept;

} // namespace detail

[[nodiscard]] std::expected<void, hooking::HookError>
AddJapaneseLocaleHooks(hooking::HookPlan& plan, gc::nesys_service::ProcessRole role) noexcept;

} // namespace gc::locale_compatibility
