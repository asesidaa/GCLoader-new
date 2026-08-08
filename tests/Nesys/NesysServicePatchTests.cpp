#include "Nesys/Launcher/NesysServiceLauncher.h"
#include "Nesys/NesysServiceProcess.h"

#include <Windows.h>
#include <iostream>
#include <string>

namespace {

int expect_true(bool actual, const char* name) {
    if (actual) {
        return 0;
    }
    std::cerr << "Expected true for " << name << "\n";
    return 1;
}

int expect_false(bool actual, const char* name) {
    if (!actual) {
        return 0;
    }
    std::cerr << "Expected false for " << name << "\n";
    return 1;
}

int expect_dword(DWORD actual, DWORD expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << "\n";
    return 1;
}

int expect_plan(
    const gc::nesys_service::NesysFeaturePlan& actual,
    const gc::nesys_service::NesysFeaturePlan& expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr
        << "Feature plan mismatch for " << name
        << ": enabled=" << actual.enabled
        << " network=" << actual.network_virtualization
        << " registry=" << actual.registry_virtualization
        << " synthetic_adapter=" << actual.synthetic_adapter
        << " server_address_override=" << actual.server_address_override
        << " registry_override=" << actual.registry_config_override
        << " thread_priority_override=" << actual.thread_priority_override
        << " launcher=" << actual.service_launcher
        << " ping=" << actual.service_ping_redirect
        << " hooks=" << actual.api_hook_count
        << "\n";
    return 1;
}

LPSTR mutable_command_line(std::string& value) {
    return value.empty() ? nullptr : value.data();
}

struct FakeChildApiState {
    int terminate_calls{0};
    int wait_calls{0};
    int resume_calls{0};
    int close_calls{0};
    DWORD resume_result{0};
};

FakeChildApiState* g_child_api = nullptr;

BOOL WINAPI fake_terminate(HANDLE, UINT) {
    ++g_child_api->terminate_calls;
    return TRUE;
}

DWORD WINAPI fake_wait(HANDLE, DWORD timeout) {
    ++g_child_api->wait_calls;
    return timeout == INFINITE ? WAIT_OBJECT_0 : WAIT_FAILED;
}

DWORD WINAPI fake_resume(HANDLE) {
    ++g_child_api->resume_calls;
    return g_child_api->resume_result;
}

BOOL WINAPI fake_close(HANDLE) {
    ++g_child_api->close_calls;
    return TRUE;
}

gc::nesys_service::ServiceChildApi fake_child_api() {
    return {
        fake_terminate,
        fake_wait,
        fake_resume,
        fake_close,
    };
}

PROCESS_INFORMATION fake_process_information() {
    return {
        reinterpret_cast<HANDLE>(0x1000),
        reinterpret_cast<HANDLE>(0x2000),
        11,
        22,
    };
}

} // namespace

int main() {
    int failures = 0;

    failures += expect_true(
        gc::nesys_service::IsNesysServiceImagePathA("NesysService.exe"),
        "bare service image");
    failures += expect_true(
        gc::nesys_service::IsNesysServiceImagePathA("C:\\Games\\GC\\NesysService.exe"),
        "absolute service image");
    failures += expect_true(
        gc::nesys_service::IsNesysServiceImagePathA("\"C:\\Games\\GC\\NesysService.exe\""),
        "quoted service image");
    failures += expect_false(
        gc::nesys_service::IsNesysServiceImagePathA("game.exe"),
        "game image is not service image");

    failures += expect_true(
        gc::nesys_service::CommandLineContainsAppArgumentA("\"NesysService.exe\" -app"),
        "quoted command line has -app");
    failures += expect_true(
        gc::nesys_service::CommandLineContainsAppArgumentA("NesysService.exe -APP"),
        "command line has uppercase -APP");
    failures += expect_false(
        gc::nesys_service::CommandLineContainsAppArgumentA("NesysService.exe -application"),
        "command line rejects -application");

    std::string service_cmd = "\"C:\\Games\\GC\\NesysService.exe\" -app";
    failures += expect_true(
        gc::nesys_service::IsNesysServiceLaunchA(nullptr, mutable_command_line(service_cmd)),
        "null application with service command");

    std::string args_only = "-app";
    failures += expect_true(
        gc::nesys_service::IsNesysServiceLaunchA("C:\\Games\\GC\\NesysService.exe", mutable_command_line(args_only)),
        "application service path with args-only command line");

    std::string wrong_image = "Other.exe -app";
    failures += expect_false(
        gc::nesys_service::IsNesysServiceLaunchA(nullptr, mutable_command_line(wrong_image)),
        "wrong image with app argument");

    std::string missing_app = "NesysService.exe";
    failures += expect_false(
        gc::nesys_service::IsNesysServiceLaunchA(nullptr, mutable_command_line(missing_app)),
        "service image without app argument");

    failures += expect_true(
        gc::nesys_service::DetectProcessRoleFromImagePathA("C:\\Games\\GC\\NesysService.exe")
            == gc::nesys_service::ProcessRole::Service,
        "service role from image path");
    std::string long_service_path = "C:\\Games\\GC\\";
    long_service_path.append(300, 'x');
    long_service_path.append("\\NesysService.exe");
    failures += expect_true(
        gc::nesys_service::DetectProcessRoleFromImagePathA(long_service_path)
            == gc::nesys_service::ProcessRole::Service,
        "service role from long image path");
    failures += expect_true(
        gc::nesys_service::DetectProcessRoleFromImagePathA("C:\\Games\\GC\\game.exe")
            == gc::nesys_service::ProcessRole::Game,
        "game role from image path");
    failures += expect_true(
        gc::nesys_service::ShouldRunGameOnlyInitialization(gc::nesys_service::ProcessRole::Game),
        "game role runs game initialization");
    failures += expect_false(
        gc::nesys_service::ShouldRunGameOnlyInitialization(gc::nesys_service::ProcessRole::Service),
        "service role skips game initialization");

    failures += expect_dword(
        gc::nesys_service::AddCreateSuspendedFlag(0),
        CREATE_SUSPENDED,
        "empty flags become suspended");
    failures += expect_dword(
        gc::nesys_service::AddCreateSuspendedFlag(CREATE_NO_WINDOW),
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        "existing flags preserve create no window");
    failures += expect_true(
        gc::nesys_service::WasCreateSuspendedRequested(CREATE_SUSPENDED),
        "detect caller requested suspended");
    failures += expect_false(
        gc::nesys_service::WasCreateSuspendedRequested(CREATE_NO_WINDOW),
        "detect caller did not request suspended");
    failures += expect_true(
        gc::nesys_service::ShouldResumeAfterServiceInjection(false),
        "resume service when caller did not request suspended");
    failures += expect_false(
        gc::nesys_service::ShouldResumeAfterServiceInjection(true),
        "preserve caller requested suspended service");

    using gc::nesys_service::FinalizeInjectedServiceChild;

    FakeChildApiState injection_failure{};
    g_child_api = &injection_failure;
    auto failed_child = fake_process_information();
    const auto failed_result = FinalizeInjectedServiceChild(
        &failed_child,
        false,
        false,
        fake_child_api());
    failures += expect_false(failed_result.success, "failed injection result");
    failures += expect_dword(
        failed_result.error,
        ERROR_DLL_INIT_FAILED,
        "failed injection error");
    failures += expect_true(
        injection_failure.terminate_calls == 1 &&
            injection_failure.wait_calls == 1 &&
            injection_failure.resume_calls == 0 &&
            injection_failure.close_calls == 2,
        "failed injection terminates waits and closes");
    failures += expect_true(
        failed_child.hProcess == nullptr &&
            failed_child.hThread == nullptr &&
            failed_child.dwProcessId == 0 &&
            failed_child.dwThreadId == 0,
        "failed injection clears process information");

    FakeChildApiState success_resume{};
    g_child_api = &success_resume;
    auto resumed_child = fake_process_information();
    const auto resumed_result = FinalizeInjectedServiceChild(
        &resumed_child,
        false,
        true,
        fake_child_api());
    failures += expect_true(resumed_result.success, "successful injection");
    failures += expect_true(
        resumed_result.resumed &&
            success_resume.resume_calls == 1 &&
            success_resume.terminate_calls == 0 &&
            success_resume.close_calls == 0,
        "successful normal launch resumes and preserves caller handles");

    FakeChildApiState success_suspended{};
    g_child_api = &success_suspended;
    auto suspended_child = fake_process_information();
    const auto suspended_result = FinalizeInjectedServiceChild(
        &suspended_child,
        true,
        true,
        fake_child_api());
    failures += expect_true(
        suspended_result.success &&
            !suspended_result.resumed &&
            success_suspended.resume_calls == 0 &&
            success_suspended.terminate_calls == 0 &&
            success_suspended.close_calls == 0,
        "successful caller-suspended launch stays suspended");

    FakeChildApiState resume_failure{};
    resume_failure.resume_result = static_cast<DWORD>(-1);
    g_child_api = &resume_failure;
    auto unresumable_child = fake_process_information();
    const auto unresumable_result = FinalizeInjectedServiceChild(
        &unresumable_child,
        false,
        true,
        fake_child_api());
    failures += expect_true(
        !unresumable_result.success &&
            resume_failure.resume_calls == 1 &&
            resume_failure.terminate_calls == 1 &&
            resume_failure.wait_calls == 1 &&
            resume_failure.close_calls == 2,
        "resume failure fails closed");

    using gc::nesys_service::NesysFeaturePlan;
    using gc::nesys_service::ProcessRole;
    using gc::nesys_service::ResolveNesysFeaturePlan;

    failures += expect_plan(
        ResolveNesysFeaturePlan(ProcessRole::Game, false, false),
        NesysFeaturePlan{
            true, false, false, false, false, false, false, true, false, 1},
        "game locale-only launcher");
    failures += expect_plan(
        ResolveNesysFeaturePlan(ProcessRole::Game, false, true),
        NesysFeaturePlan{
            true, false, true, false, false, true, false, true, false, 4},
        "game registry-only");
    failures += expect_plan(
        ResolveNesysFeaturePlan(ProcessRole::Game, true, false),
        NesysFeaturePlan{
            true, true, false, true, true, false, true, true, false, 7},
        "game network-only");
    failures += expect_plan(
        ResolveNesysFeaturePlan(ProcessRole::Game, true, true),
        NesysFeaturePlan{
            true, true, true, true, true, true, true, true, false, 10},
        "game combined");

    failures += expect_plan(
        ResolveNesysFeaturePlan(ProcessRole::Service, false, false),
        NesysFeaturePlan{
            false, false, false, false, false, false, false, false, false, 0},
        "service network-off registry-off");
    failures += expect_plan(
        ResolveNesysFeaturePlan(ProcessRole::Service, false, true),
        NesysFeaturePlan{
            true, false, true, false, false, true, false, false, false, 4},
        "service registry-only");
    failures += expect_plan(
        ResolveNesysFeaturePlan(ProcessRole::Service, true, false),
        NesysFeaturePlan{
            true, true, false, true, true, false, true, false, true, 12},
        "service network-only");
    failures += expect_plan(
        ResolveNesysFeaturePlan(ProcessRole::Service, true, true),
        NesysFeaturePlan{
            true, true, true, true, true, true, true, false, true, 15},
        "service combined");

    return failures == 0 ? 0 : 1;
}
