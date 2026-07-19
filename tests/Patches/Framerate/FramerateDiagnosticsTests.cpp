#include "Patches/Framerate/FramerateDiagnostics.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

struct DiagnosticState {
    std::vector<std::string> infos;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    std::vector<std::string> messages;
    std::vector<DWORD> termination_codes;
    std::vector<std::string> fatal_sequence;
    std::uint32_t fail_fast_calls{};
};

DiagnosticState* g_state = nullptr;

void LogInfo(const char* text) { g_state->infos.emplace_back(text); }
void LogWarning(const char* text) { g_state->warnings.emplace_back(text); }
void LogError(const char* text) {
    g_state->errors.emplace_back(text);
    g_state->fatal_sequence.emplace_back("log");
}
void ShowError(const char* text) {
    g_state->messages.emplace_back(text);
    g_state->fatal_sequence.emplace_back("modal");
}
void Terminate(DWORD code) {
    g_state->termination_codes.push_back(code);
    g_state->fatal_sequence.emplace_back("terminate");
}
void FailFast() {
    ++g_state->fail_fast_calls;
    g_state->fatal_sequence.emplace_back("fail_fast");
}

gc::framerate::FrameratePlatformActions Actions() {
    return {LogInfo, LogWarning, LogError, ShowError, Terminate, FailFast};
}

bool Contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

int main() {
using namespace gc::framerate;
int failures = 0;

DiagnosticState validated_startup;
g_state = &validated_startup;
const FramerateStartupPatchSummary transformed_summary{
    .direct_write_count = 17,
    .hook_count = 41,
    .menu_repeat_initial = 38,
    .menu_repeat_interval = 7,
    .authored_frame_milliseconds = 1000.0F / 60.0F,
};
ReportFramerateStartup(
    FramerateProfile::Create(144).value(),
    transformed_summary,
    Actions());
failures += Expect(
    validated_startup.infos.size() == 1 &&
        validated_startup.warnings.empty(),
    "validated target logs no support warning");
failures += Expect(
    Contains(validated_startup.infos[0], "mode=transformed") &&
        Contains(
            validated_startup.infos[0],
            "authored_clock=deterministic_phase") &&
        Contains(validated_startup.infos[0], "direct_writes=17") &&
        Contains(validated_startup.infos[0], "hooks=41") &&
        Contains(validated_startup.infos[0], "menu_repeat=38/7") &&
        Contains(
            validated_startup.infos[0],
            "news_notice_updates=native") &&
        Contains(validated_startup.infos[0], "ifbl_loops=original") &&
        Contains(validated_startup.infos[0], "player_decrement=native") &&
        Contains(
            validated_startup.infos[0],
            "countdown_asset=authored60") &&
        Contains(
            validated_startup.infos[0],
            "player_duration=dynamic_scaled"),
    "transformed startup logs complete timing ownership");

DiagnosticState native_startup;
g_state = &native_startup;
ReportFramerateStartup(
    FramerateProfile::Create(60).value(),
    FramerateStartupPatchSummary{
        .direct_write_count = 0,
        .hook_count = 1,
        .menu_repeat_initial = 16,
        .menu_repeat_interval = 3,
        .authored_frame_milliseconds = 1000.0F / 60.0F,
    },
    Actions());
failures += Expect(
    Contains(native_startup.infos[0], "mode=native") &&
        Contains(native_startup.infos[0], "authored_clock=native_bypass") &&
        Contains(native_startup.infos[0], "direct_writes=0") &&
        Contains(native_startup.infos[0], "hooks=1"),
    "native startup reports bypass and one cap hook");

DiagnosticState formula_startup;
g_state = &formula_startup;
ReportFramerateStartup(
    FramerateProfile::Create(200).value(),
    FramerateStartupPatchSummary{
        .direct_write_count = 17,
        .hook_count = 41,
        .menu_repeat_initial = 53,
        .menu_repeat_interval = 10,
        .authored_frame_milliseconds = 1000.0F / 60.0F,
    },
    Actions());
failures += Expect(
    formula_startup.infos.size() == 1 &&
        formula_startup.warnings.size() == 1 &&
        Contains(formula_startup.warnings[0], "not individually gameplay-validated"),
    "formula-only target logs exactly one support warning");

FramerateObservation measured120{
    .decision = FramerateDecision::FatalMismatch,
    .target_fps = 144,
    .measured_fps = 120.0,
    .relative_error = 1.0 / 6.0,
    .interval_count = 240,
    .mismatching_streak = 3,
};
FramerateObservation measured60 = measured120;
measured60.measured_fps = 60.0;
measured60.relative_error = 7.0 / 12.0;

failures += Expect(
    !ShouldSuggestIntervalModeOne(144, 120.0),
    "144 versus 120 omits IntervalMode");
failures += Expect(
    ShouldSuggestIntervalModeOne(144, 60.0),
    "144 versus 60 includes IntervalMode");
failures += Expect(
    !ShouldSuggestIntervalModeOne(60, 60.0),
    "native target never receives high-target hint");
failures += Expect(
    ShouldSuggestIntervalModeOne(144, 61.8) &&
        !ShouldSuggestIntervalModeOne(144, 61.81),
    "60-FPS hint uses inclusive three-percent tolerance");

DiagnosticState ordinary;
g_state = &ordinary;
std::atomic_bool ordinary_latch{false};
ReportFramerateMismatch(measured120, ordinary_latch, Actions());
failures += Expect(
    ordinary.errors.size() == 1 &&
        Contains(ordinary.errors[0], "target_fps=144") &&
        Contains(ordinary.errors[0], "measured_fps=120") &&
        Contains(ordinary.errors[0], "relative_error=") &&
        Contains(ordinary.errors[0], "interval_count=240") &&
        Contains(ordinary.errors[0], "failed_windows=3"),
    "fatal log contains required measurements");
failures += Expect(
    ordinary.messages.size() == 1 &&
        !Contains(ordinary.messages[0], "IntervalMode"),
    "ordinary mismatch modal omits IntervalMode");
failures += Expect(
    ordinary.termination_codes == std::vector<DWORD>{ERROR_INVALID_DATA} &&
        ordinary.fail_fast_calls == 1 &&
        ordinary.fatal_sequence == std::vector<std::string>{
            "log", "modal", "terminate", "fail_fast"},
    "fatal mismatch terminates then fail-fasts");

ReportFramerateMismatch(measured120, ordinary_latch, Actions());
failures += Expect(
    ordinary.messages.size() == 1 && ordinary.errors.size() == 1,
    "atomic latch suppresses duplicate publication");

DiagnosticState sixty;
g_state = &sixty;
std::atomic_bool sixty_latch{false};
ReportFramerateMismatch(measured60, sixty_latch, Actions());
failures += Expect(
    sixty.messages.size() == 1 &&
        Contains(sixty.messages[0], "IntervalMode = 1"),
    "approximately-60 mismatch contains conditional hint");

DiagnosticState initialization;
g_state = &initialization;
std::atomic_bool initialization_latch{false};
ReportFramerateInitializationFailure(
    "hook install failed at palette compare; rollback_complete=true",
    initialization_latch,
    Actions());
failures += Expect(
    initialization.errors.size() == 1 &&
        Contains(initialization.errors[0], "rollback_complete=true") &&
        initialization.messages.size() == 1 &&
        initialization.termination_codes ==
            std::vector<DWORD>{ERROR_DLL_INIT_FAILED} &&
        initialization.fail_fast_calls == 1,
    "initialization failure logs, prompts, terminates, and fail-fasts");

DiagnosticState runtime;
g_state = &runtime;
std::atomic_bool runtime_latch{false};
ReportFramerateRuntimeFailure(
    "IFBL wait scaling overflow",
    runtime_latch,
    Actions());
failures += Expect(
    runtime.errors.size() == 1 &&
        Contains(runtime.errors[0], "IFBL wait scaling overflow") &&
        runtime.messages.size() == 1 &&
        runtime.termination_codes ==
            std::vector<DWORD>{ERROR_INVALID_DATA} &&
        runtime.fail_fast_calls == 1,
    "runtime conversion failure is fatal and one-shot");

ReportFramerateRuntimeFailure(
    "second injected transform failure",
    runtime_latch,
    Actions());
failures += Expect(
    runtime.errors.size() == 1 &&
        runtime.messages.size() == 1 &&
        runtime.termination_codes.size() == 1 &&
        runtime.fail_fast_calls == 1,
    "runtime transform failure publication is one-shot");

return failures == 0 ? 0 : 1;
}
