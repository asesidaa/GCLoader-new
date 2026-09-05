#pragma once
#include "Platform/Win32/Hooking/HookPlan.h"
#include "Rfid/Runtime.h"
namespace gc::rfid {
// Exclusive COM exports: lifetime is process-wide; registry owns the hooks.
class Win32ComHooks final {
public:
    explicit Win32ComHooks(Runtime& runtime) noexcept : rfid_(runtime) {}
    [[nodiscard]] std::expected<void, hooking::HookError> AddHooks(hooking::HookPlan&) noexcept;
private:
    static Win32ComHooks* active_;
    Runtime& rfid_;
    decltype(&::GetCommModemStatus) original_get_comm_modem_status_{};
    BOOL GetCommModemStatus(HANDLE file, LPDWORD modem_status);
    static BOOL WINAPI GetCommModemStatusDetour(HANDLE file, LPDWORD modem_status) noexcept;
    decltype(&::EscapeCommFunction) original_escape_comm_function_{};
    BOOL EscapeCommFunction(HANDLE file, DWORD function);
    static BOOL WINAPI EscapeCommFunctionDetour(HANDLE file, DWORD function) noexcept;
    decltype(&::ClearCommError) original_clear_comm_error_{};
    BOOL ClearCommError(HANDLE file, LPDWORD errors, LPCOMSTAT status);
    static BOOL WINAPI ClearCommErrorDetour(HANDLE file, LPDWORD errors, LPCOMSTAT status) noexcept;
    decltype(&::SetCommMask) original_set_comm_mask_{};
    BOOL SetCommMask(HANDLE file, DWORD event_mask);
    static BOOL WINAPI SetCommMaskDetour(HANDLE file, DWORD event_mask) noexcept;
    decltype(&::SetupComm) original_setup_comm_{};
    BOOL SetupComm(HANDLE file, DWORD input_queue, DWORD output_queue);
    static BOOL WINAPI SetupCommDetour(HANDLE file, DWORD input_queue, DWORD output_queue) noexcept;
    decltype(&::GetCommState) original_get_comm_state_{};
    BOOL GetCommState(HANDLE file, LPDCB dcb);
    static BOOL WINAPI GetCommStateDetour(HANDLE file, LPDCB dcb) noexcept;
    decltype(&::SetCommState) original_set_comm_state_{};
    BOOL SetCommState(HANDLE file, LPDCB dcb);
    static BOOL WINAPI SetCommStateDetour(HANDLE file, LPDCB dcb) noexcept;
    decltype(&::SetCommTimeouts) original_set_comm_timeouts_{};
    BOOL SetCommTimeouts(HANDLE file, LPCOMMTIMEOUTS timeouts);
    static BOOL WINAPI SetCommTimeoutsDetour(HANDLE file, LPCOMMTIMEOUTS timeouts) noexcept;
    decltype(&::GetCommTimeouts) original_get_comm_timeouts_{};
    BOOL GetCommTimeouts(HANDLE file, LPCOMMTIMEOUTS timeouts);
    static BOOL WINAPI GetCommTimeoutsDetour(HANDLE file, LPCOMMTIMEOUTS timeouts) noexcept;
};
}
