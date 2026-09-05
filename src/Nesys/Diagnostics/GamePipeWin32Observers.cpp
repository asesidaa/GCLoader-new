#include "Nesys/Diagnostics/GamePipeWin32Observers.h"
#include "Nesys/Diagnostics/RequestPipelineDiagnostics.h"

namespace gc::nesys_service::diagnostics {
namespace {
using namespace win32_hooks;
void BeforeOpenA(void*, const CreateFileAContext& context, ObservationState& observation) noexcept {
    observation.active = !context.path_claimed && IsNesysPipeNameA(context.original_path);
    if (observation.active) observation.started_ms = MonotonicMilliseconds();
}
void BeforeOpenW(void*, const CreateFileWContext& context, ObservationState& observation) noexcept {
    observation.active = !context.path_claimed && IsNesysPipeNameW(context.original_path);
    if (observation.active) observation.started_ms = MonotonicMilliseconds();
}
void AfterOpenA(void*, const CreateFileAContext& context, const CallOutcome<HANDLE>& outcome,
                const ObservationState& observation) noexcept {
    if (observation.active) ObserveGamePipeOpenA(context.original_path, outcome.result,
        observation.started_ms, MonotonicMilliseconds(), outcome.last_error);
}
void AfterOpenW(void*, const CreateFileWContext& context, const CallOutcome<HANDLE>& outcome,
                const ObservationState& observation) noexcept {
    if (observation.active) ObserveGamePipeOpenW(context.original_path, outcome.result,
        observation.started_ms, MonotonicMilliseconds(), outcome.last_error);
}
void BeforeWrite(void*, const WriteFileContext& context, ObservationState& observation) noexcept {
    observation.active = IsTrackedNesysPipeHandle(context.file);
    if (observation.active) observation.started_ms = MonotonicMilliseconds();
}
void AfterWrite(void*, const WriteFileContext& context, const CallOutcome<BOOL>& outcome,
                const ObservationState& observation) noexcept {
    if (observation.active) ObserveGamePipeWrite(context.file, context.buffer, context.bytes_to_write,
        outcome.result, outcome.last_error, observation.started_ms, MonotonicMilliseconds());
}
void BeforeFlush(void*, const FlushFileBuffersContext& context, ObservationState& observation) noexcept {
    observation.active = IsTrackedNesysPipeHandle(context.file);
    if (observation.active) observation.started_ms = MonotonicMilliseconds();
}
void AfterFlush(void*, const FlushFileBuffersContext& context, const CallOutcome<BOOL>& outcome,
                const ObservationState& observation) noexcept {
    if (observation.active) ObserveGamePipeFlush(context.file, outcome.result, outcome.last_error,
        observation.started_ms, MonotonicMilliseconds());
}
void BeforeClose(void*, const CloseHandleContext& context, ObservationState& observation) noexcept {
    observation.active = IsTrackedNesysPipeHandle(context.object);
}
void AfterClose(void*, const CloseHandleContext& context, const CallOutcome<BOOL>&,
                const ObservationState& observation) noexcept {
    // Forget after every native close attempt, including a failed attempt.
    if (observation.active) ObserveGameHandleClose(context.object);
}
}
std::expected<void, win32_hooks::RegistrationError> AddGamePipeWin32Observers(
    win32_hooks::Kernel32Dispatcher& dispatcher) noexcept {
    if (const auto result = dispatcher.create_file_a.AddPost(
        {"NESYS", "CreateFileA"}, nullptr, BeforeOpenA, AfterOpenA); !result) return result;
    if (const auto result = dispatcher.create_file_w.AddPost(
        {"NESYS", "CreateFileW"}, nullptr, BeforeOpenW, AfterOpenW); !result) return result;
    if (const auto result = dispatcher.write_file.AddPost(
        {"NESYS", "WriteFile"}, nullptr, BeforeWrite, AfterWrite); !result) return result;
    if (const auto result = dispatcher.flush_file_buffers.AddPost(
        {"NESYS", "FlushFileBuffers"}, nullptr, BeforeFlush, AfterFlush); !result) return result;
    if (const auto result = dispatcher.close_handle.AddPost(
        {"NESYS", "CloseHandle"}, nullptr, BeforeClose, AfterClose); !result) return result;
    return {};
}
}
