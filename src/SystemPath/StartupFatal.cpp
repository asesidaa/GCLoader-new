#include "SystemPath/StartupFatal.h"

#include <cstdlib>
#include <string>

#include "plog/Log.h"

namespace gc::system_path {

namespace {

constexpr char kFallbackLog[] =
    "GCLoader startup failed before initialization completed";
constexpr wchar_t kFallbackModal[] =
    L"GCLoader could not complete startup. Check the loader log for details.";
constexpr wchar_t kFatalTitle[] = L"GCLoader startup error";

void ProductionLogError(void*, const char* text) noexcept {
    try {
        PLOG_ERROR << (text == nullptr ? kFallbackLog : text);
    } catch (...) {
    }
}

void ProductionShowError(
    void*,
    const wchar_t* text,
    const wchar_t* title) noexcept {
    MessageBoxW(
        nullptr,
        text == nullptr ? kFallbackModal : text,
        title == nullptr ? kFatalTitle : title,
        MB_OK | MB_ICONERROR);
}

void ProductionTerminateProcess(void*, DWORD exit_code) noexcept {
    TerminateProcess(GetCurrentProcess(), exit_code);
}

void ProductionFailFast(void*) noexcept {
    RaiseFailFastException(nullptr, nullptr, 0);
    std::abort();
}

} // namespace

StartupFatalActions ProductionStartupFatalActions() noexcept {
    return {
        .log_error = &ProductionLogError,
        .show_error = &ProductionShowError,
        .terminate_process = &ProductionTerminateProcess,
        .fail_fast = &ProductionFailFast,
    };
}

void PublishStartupFatal(
    std::atomic_bool& latch,
    std::string_view log,
    std::wstring_view modal,
    DWORD exit_code,
    StartupFatalActions actions) noexcept {
    PublishStartupFatal(
        latch,
        log,
        modal,
        kFatalTitle,
        exit_code,
        actions);
}

void PublishStartupFatal(
    std::atomic_bool& latch,
    std::string_view log,
    std::wstring_view modal,
    std::wstring_view title,
    DWORD exit_code,
    StartupFatalActions actions) noexcept {
    if (latch.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    const char* log_text = kFallbackLog;
    std::string log_storage;
    try {
        log_storage.assign(log);
        log_text = log_storage.c_str();
    } catch (...) {
    }

    const wchar_t* title_text = kFatalTitle;
    std::wstring title_storage;
    try {
        title_storage.assign(title);
        title_text = title_storage.c_str();
    } catch (...) {
    }

    const wchar_t* modal_text = kFallbackModal;
    std::wstring modal_storage;
    try {
        modal_storage.assign(modal);
        modal_text = modal_storage.c_str();
    } catch (...) {
    }

    if (actions.log_error != nullptr) {
        actions.log_error(actions.context, log_text);
    }
    if (actions.show_error != nullptr) {
        actions.show_error(
            actions.context,
            modal_text,
            title_text);
    }
    if (actions.terminate_process != nullptr) {
        actions.terminate_process(actions.context, exit_code);
    }
    if (actions.fail_fast != nullptr) {
        actions.fail_fast(actions.context);
    }
}

} // namespace gc::system_path
