#include "Rfid/Win32ComHooks.h"
#include "plog/Log.h"
#include <iomanip>
namespace gc::rfid {
namespace {
BOOL Fail(DWORD error) noexcept { SetLastError(error); return FALSE; }
}
Win32ComHooks* Win32ComHooks::active_{};
std::expected<void, hooking::HookError> Win32ComHooks::AddHooks(hooking::HookPlan& plan) noexcept {
    if (active_) return std::unexpected(hooking::HookError{
        .stage = hooking::HookStage::invalid_plan, .identity = {"RFID", "COM hooks"},
        .win32_error = ERROR_ALREADY_INITIALIZED});
    active_ = this;
    if (const auto result = plan.AddInlineExport(
        {"RFID", "GetCommModemStatus"}, {L"kernel32.dll", "GetCommModemStatus"},
        GetCommModemStatusDetour, &original_get_comm_modem_status_); !result) return result;
    if (const auto result = plan.AddInlineExport(
        {"RFID", "EscapeCommFunction"}, {L"kernel32.dll", "EscapeCommFunction"},
        EscapeCommFunctionDetour, &original_escape_comm_function_); !result) return result;
    if (const auto result = plan.AddInlineExport(
        {"RFID", "ClearCommError"}, {L"kernel32.dll", "ClearCommError"},
        ClearCommErrorDetour, &original_clear_comm_error_); !result) return result;
    if (const auto result = plan.AddInlineExport(
        {"RFID", "SetCommMask"}, {L"kernel32.dll", "SetCommMask"},
        SetCommMaskDetour, &original_set_comm_mask_); !result) return result;
    if (const auto result = plan.AddInlineExport(
        {"RFID", "SetupComm"}, {L"kernel32.dll", "SetupComm"},
        SetupCommDetour, &original_setup_comm_); !result) return result;
    if (const auto result = plan.AddInlineExport(
        {"RFID", "GetCommState"}, {L"kernel32.dll", "GetCommState"},
        GetCommStateDetour, &original_get_comm_state_); !result) return result;
    if (const auto result = plan.AddInlineExport(
        {"RFID", "SetCommState"}, {L"kernel32.dll", "SetCommState"},
        SetCommStateDetour, &original_set_comm_state_); !result) return result;
    if (const auto result = plan.AddInlineExport(
        {"RFID", "SetCommTimeouts"}, {L"kernel32.dll", "SetCommTimeouts"},
        SetCommTimeoutsDetour, &original_set_comm_timeouts_); !result) return result;
    if (const auto result = plan.AddInlineExport(
        {"RFID", "GetCommTimeouts"}, {L"kernel32.dll", "GetCommTimeouts"},
        GetCommTimeoutsDetour, &original_get_comm_timeouts_); !result) return result;
    return {};
}
BOOL Win32ComHooks::GetCommModemStatus(
    HANDLE file,
    LPDWORD modem_status)
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return original_get_comm_modem_status_(file, modem_status);
    }
    if (modem_status == nullptr) {
        PLOG_ERROR
            << "RFID COM2 trace api=GetCommModemStatus result=failure error="
            << ERROR_INVALID_PARAMETER;
        return Fail(ERROR_INVALID_PARAMETER);
    }
    *modem_status = rfid_.port().ModemStatus();
    return TRUE;
}
BOOL WINAPI Win32ComHooks::GetCommModemStatusDetour(HANDLE file, LPDWORD modem_status) noexcept {
    try { return active_->GetCommModemStatus(file, modem_status); }
    catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); return FALSE; }
}
BOOL Win32ComHooks::EscapeCommFunction(
    HANDLE file,
    DWORD function)
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return original_escape_comm_function_(file, function);
    }
    const auto result = rfid_.port().EscapeCommFunction(function);
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=EscapeCommFunction result=failure"
            << " function=" << function
            << " error=" << result.error();
        return Fail(result.error());
    }
    return TRUE;
}
BOOL WINAPI Win32ComHooks::EscapeCommFunctionDetour(HANDLE file, DWORD function) noexcept {
    try { return active_->EscapeCommFunction(file, function); }
    catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); return FALSE; }
}
BOOL Win32ComHooks::ClearCommError(
    HANDLE file,
    LPDWORD errors,
    LPCOMSTAT status)
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return original_clear_comm_error_(file, errors, status);
    }
    if (errors != nullptr) {
        *errors = 0;
    }
    if (status != nullptr) {
        *status = rfid_.port().CommStatus();
    }
    return TRUE;
}
BOOL WINAPI Win32ComHooks::ClearCommErrorDetour(HANDLE file, LPDWORD errors, LPCOMSTAT status) noexcept {
    try { return active_->ClearCommError(file, errors, status); }
    catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); return FALSE; }
}
BOOL Win32ComHooks::SetCommMask(HANDLE file, DWORD event_mask)
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return original_set_comm_mask_(file, event_mask);
    }
    const auto result = rfid_.port().SetCommMask(event_mask);
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommMask result=failure mask=0x"
            << std::hex << event_mask << std::dec
            << " error=" << result.error();
        return Fail(result.error());
    }
    return TRUE;
}
BOOL WINAPI Win32ComHooks::SetCommMaskDetour(HANDLE file, DWORD event_mask) noexcept {
    try { return active_->SetCommMask(file, event_mask); }
    catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); return FALSE; }
}
BOOL Win32ComHooks::SetupComm(
    HANDLE file,
    DWORD input_queue,
    DWORD output_queue)
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return original_setup_comm_(file, input_queue, output_queue);
    }
    const auto result = rfid_.port().SetupComm(input_queue, output_queue);
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetupComm result=failure"
            << " input_queue=" << input_queue
            << " output_queue=" << output_queue
            << " error=" << result.error();
        return Fail(result.error());
    }
    return TRUE;
}
BOOL WINAPI Win32ComHooks::SetupCommDetour(HANDLE file, DWORD input_queue, DWORD output_queue) noexcept {
    try { return active_->SetupComm(file, input_queue, output_queue); }
    catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); return FALSE; }
}
BOOL Win32ComHooks::GetCommState(HANDLE file, LPDCB dcb)
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return original_get_comm_state_(file, dcb);
    }
    if (dcb == nullptr) {
        PLOG_ERROR
            << "RFID COM2 trace api=GetCommState result=failure error="
            << ERROR_INVALID_PARAMETER;
        return Fail(ERROR_INVALID_PARAMETER);
    }
    *dcb = rfid_.port().GetCommState();
    return TRUE;
}
BOOL WINAPI Win32ComHooks::GetCommStateDetour(HANDLE file, LPDCB dcb) noexcept {
    try { return active_->GetCommState(file, dcb); }
    catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); return FALSE; }
}
BOOL Win32ComHooks::SetCommState(HANDLE file, LPDCB dcb)
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return original_set_comm_state_(file, dcb);
    }
    if (dcb == nullptr) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommState result=failure error="
            << ERROR_INVALID_PARAMETER << " dcb=<null>";
        return Fail(ERROR_INVALID_PARAMETER);
    }
    const auto result = rfid_.port().SetCommState(*dcb);
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommState result=failure"
            << " error=" << result.error()
            << " length=" << dcb->DCBlength
            << " baud=" << dcb->BaudRate
            << " binary=" << dcb->fBinary
            << " parity_check=" << dcb->fParity
            << " outx_cts=" << dcb->fOutxCtsFlow
            << " outx_dsr=" << dcb->fOutxDsrFlow
            << " dtr_control=" << dcb->fDtrControl
            << " dsr_sensitivity=" << dcb->fDsrSensitivity
            << " out_x=" << dcb->fOutX
            << " in_x=" << dcb->fInX
            << " rts_control=" << dcb->fRtsControl
            << " byte_size=" << static_cast<unsigned int>(dcb->ByteSize)
            << " parity=" << static_cast<unsigned int>(dcb->Parity)
            << " stop_bits=" << static_cast<unsigned int>(dcb->StopBits);
        return Fail(result.error());
    }
    return TRUE;
}
BOOL WINAPI Win32ComHooks::SetCommStateDetour(HANDLE file, LPDCB dcb) noexcept {
    try { return active_->SetCommState(file, dcb); }
    catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); return FALSE; }
}
BOOL Win32ComHooks::SetCommTimeouts(
    HANDLE file,
    LPCOMMTIMEOUTS timeouts)
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return original_set_comm_timeouts_(file, timeouts);
    }
    if (timeouts == nullptr) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommTimeouts result=failure error="
            << ERROR_INVALID_PARAMETER;
        return Fail(ERROR_INVALID_PARAMETER);
    }
    const auto result = rfid_.port().SetCommTimeouts(*timeouts);
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommTimeouts result=failure error="
            << result.error();
        return Fail(result.error());
    }
    return TRUE;
}
BOOL WINAPI Win32ComHooks::SetCommTimeoutsDetour(HANDLE file, LPCOMMTIMEOUTS timeouts) noexcept {
    try { return active_->SetCommTimeouts(file, timeouts); }
    catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); return FALSE; }
}
BOOL Win32ComHooks::GetCommTimeouts(
    HANDLE file,
    LPCOMMTIMEOUTS timeouts)
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return original_get_comm_timeouts_(file, timeouts);
    }
    if (timeouts == nullptr) {
        PLOG_ERROR
            << "RFID COM2 trace api=GetCommTimeouts result=failure error="
            << ERROR_INVALID_PARAMETER;
        return Fail(ERROR_INVALID_PARAMETER);
    }
    *timeouts = rfid_.port().GetCommTimeouts();
    return TRUE;
}
BOOL WINAPI Win32ComHooks::GetCommTimeoutsDetour(HANDLE file, LPCOMMTIMEOUTS timeouts) noexcept {
    try { return active_->GetCommTimeouts(file, timeouts); }
    catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); return FALSE; }
}
}
