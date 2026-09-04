#include "Diagnostics/FatalProcess.h"

#include <Windows.h>
#include <atomic>
#include <cstdlib>

#include "plog/Log.h"

namespace gc::diagnostics {

[[noreturn]] void AbortProcess(FatalProcessReport report) noexcept {
    static std::atomic_flag published = ATOMIC_FLAG_INIT;
    if (!published.test_and_set(std::memory_order_acq_rel)) {
        constexpr char fallback_log[] = "GCLoader could not complete startup";
        constexpr wchar_t fallback_modal[] =
            L"GCLoader could not continue. Check the process loader log for details.";
        constexpr wchar_t fallback_title[] = L"GCLoader fatal error";
        try {
            PLOG_ERROR << (report.log.empty() ? fallback_log : report.log.c_str());
        } catch (...) {
            OutputDebugStringA(fallback_log);
        }
        MessageBoxW(
            nullptr,
            report.modal.empty() ? fallback_modal : report.modal.c_str(),
            report.title.empty() ? fallback_title : report.title.c_str(),
            MB_OK | MB_ICONERROR);
    }
    std::abort();
}

} // namespace gc::diagnostics
