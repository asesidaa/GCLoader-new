#include "WasapiAudioPatch.h"
#include "WasapiAudioPatchInternal.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using gc::audio::AudioHookFailure;
using gc::audio::AudioHookStage;

struct FakeState {
    HMODULE module{reinterpret_cast<HMODULE>(0x1000)};
    FARPROC export_address{reinterpret_cast<FARPROC>(0x2000)};
    MH_STATUS initialize_status{MH_OK};
    MH_STATUS create_status{MH_OK};
    MH_STATUS queue_status{MH_OK};
    MH_STATUS apply_status{MH_OK};
    int module_calls{0};
    int export_calls{0};
    int initialize_calls{0};
    int apply_calls{0};
    int engine_factory_calls{0};
    std::wstring module_name;
    std::string export_name;
    std::vector<LPVOID> created;
    std::vector<LPVOID> queued;
    std::vector<LPVOID> disabled;
    std::vector<LPVOID> removed;
    LPVOID detour{};
    LPVOID* original_storage{};
};

FakeState* g_fake{};

HMODULE WINAPI fake_get_module_handle(LPCWSTR module_name) {
    ++g_fake->module_calls;
    g_fake->module_name = module_name == nullptr ? L"" : module_name;
    return g_fake->module;
}

FARPROC WINAPI fake_get_proc_address(HMODULE module, LPCSTR export_name) {
    ++g_fake->export_calls;
    if (module != g_fake->module) {
        return nullptr;
    }
    g_fake->export_name = export_name == nullptr ? "" : export_name;
    return g_fake->export_address;
}

MH_STATUS WINAPI fake_initialize() {
    ++g_fake->initialize_calls;
    return g_fake->initialize_status;
}

MH_STATUS WINAPI fake_create(LPVOID target, LPVOID detour, LPVOID* original) {
    g_fake->created.push_back(target);
    g_fake->detour = detour;
    g_fake->original_storage = original;
    if (g_fake->create_status == MH_OK && original != nullptr) {
        *original = reinterpret_cast<LPVOID>(0x3000);
    }
    return g_fake->create_status;
}

MH_STATUS WINAPI fake_queue_enable(LPVOID target) {
    g_fake->queued.push_back(target);
    return g_fake->queue_status;
}

MH_STATUS WINAPI fake_apply_queued() {
    ++g_fake->apply_calls;
    return g_fake->apply_status;
}

MH_STATUS WINAPI fake_disable(LPVOID target) {
    g_fake->disabled.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI fake_remove(LPVOID target) {
    g_fake->removed.push_back(target);
    return MH_OK;
}

gc::audio::AudioMinHookApi fake_minhook_api() {
    return {
        fake_initialize,
        fake_create,
        fake_queue_enable,
        fake_apply_queued,
        fake_disable,
        fake_remove,
    };
}

gc::audio::detail::AudioResolverApi fake_resolver_api() {
    return {fake_get_module_handle, fake_get_proc_address};
}

bool install(
    bool enabled,
    FakeState& state,
    AudioHookFailure* failure) {
    g_fake = &state;
    return gc::audio::detail::InstallWasapiAudioHookWithResolver(
        enabled,
        fake_minhook_api(),
        fake_resolver_api(),
        failure);
}

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

bool only_target(const std::vector<LPVOID>& values, LPVOID target) {
    return values == std::vector<LPVOID>{target};
}

bool never_all_hooks(const FakeState& state) {
    LPVOID all_hooks = MH_ALL_HOOKS;
    const auto contains_all_hooks = [all_hooks](
                                        const std::vector<LPVOID>& values) {
        return std::find(values.begin(), values.end(), all_hooks) !=
            values.end();
    };
    return !contains_all_hooks(state.created) &&
        !contains_all_hooks(state.queued) &&
        !contains_all_hooks(state.disabled) &&
        !contains_all_hooks(state.removed);
}

int expect_failure(
    const AudioHookFailure& actual,
    AudioHookStage stage,
    MH_STATUS status,
    DWORD win32_error,
    LPVOID target,
    const char* name) {
    int failures = 0;
    failures += expect(actual.stage == stage, name);
    failures += expect(actual.status == status, name);
    failures += expect(actual.win32_error == win32_error, name);
    failures += expect(actual.target == target, name);
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    const auto target = reinterpret_cast<LPVOID>(0x2000);

    FakeState disabled{};
    AudioHookFailure disabled_failure{
        AudioHookStage::ApplyQueued,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_ACCESS_DENIED,
        target,
    };
    failures += expect(
        install(false, disabled, &disabled_failure),
        "disabled install succeeds");
    failures += expect(
        disabled.module_calls == 0 && disabled.export_calls == 0,
        "disabled mode performs zero resolution");
    failures += expect(
        disabled.initialize_calls == 0 && disabled.created.empty() &&
            disabled.queued.empty() && disabled.apply_calls == 0 &&
            disabled.disabled.empty() && disabled.removed.empty(),
        "disabled mode performs zero MinHook calls");
    failures += expect(
        disabled.engine_factory_calls == 0,
        "disabled mode performs zero engine calls");
    failures += expect_failure(
        disabled_failure,
        AudioHookStage::None,
        MH_OK,
        ERROR_SUCCESS,
        nullptr,
        "disabled mode clears failure");

    FakeState success{};
    success.initialize_status = MH_ERROR_ALREADY_INITIALIZED;
    AudioHookFailure success_failure{};
    failures += expect(
        install(true, success, &success_failure),
        "already initialized MinHook is accepted");
    failures += expect(
        success.module_calls == 1 && success.module_name == L"dsound.dll",
        "resolve exact loaded dsound module");
    failures += expect(
        success.export_calls == 1 &&
            success.export_name == "DirectSoundCreate8",
        "resolve exact DirectSoundCreate8 export");
    failures += expect(
        only_target(success.created, target) &&
            only_target(success.queued, target) && success.apply_calls == 1,
        "create queue and apply exact resolved target");
    failures += expect(
        success.detour != nullptr && success.original_storage != nullptr,
        "create receives detour and original storage");
    failures += expect(
        success.disabled.empty() && success.removed.empty(),
        "successful install retains committed target");
    failures += expect(
        never_all_hooks(success),
        "successful install never uses MH_ALL_HOOKS");
    failures += expect(
        success.engine_factory_calls == 0,
        "hook installation performs no engine calls");
    failures += expect_failure(
        success_failure,
        AudioHookStage::None,
        MH_OK,
        ERROR_SUCCESS,
        nullptr,
        "successful install clears failure");

    FakeState missing_module{};
    missing_module.module = nullptr;
    AudioHookFailure missing_module_failure{};
    failures += expect(
        !install(true, missing_module, &missing_module_failure),
        "missing module fails");
    failures += expect_failure(
        missing_module_failure,
        AudioHookStage::ResolveModule,
        MH_OK,
        ERROR_MOD_NOT_FOUND,
        nullptr,
        "missing module failure details");
    failures += expect(
        missing_module.export_calls == 0 &&
            missing_module.initialize_calls == 0,
        "missing module stops before export and MinHook");

    FakeState missing_export{};
    missing_export.export_address = nullptr;
    AudioHookFailure missing_export_failure{};
    failures += expect(
        !install(true, missing_export, &missing_export_failure),
        "missing export fails");
    failures += expect_failure(
        missing_export_failure,
        AudioHookStage::ResolveExport,
        MH_OK,
        ERROR_PROC_NOT_FOUND,
        nullptr,
        "missing export failure details");
    failures += expect(
        missing_export.initialize_calls == 0,
        "missing export stops before MinHook");

    FakeState initialize_failure{};
    initialize_failure.initialize_status = MH_ERROR_MEMORY_ALLOC;
    AudioHookFailure initialize_error{};
    failures += expect(
        !install(true, initialize_failure, &initialize_error),
        "initialize failure propagates");
    failures += expect_failure(
        initialize_error,
        AudioHookStage::InitializeMinHook,
        MH_ERROR_MEMORY_ALLOC,
        ERROR_SUCCESS,
        target,
        "initialize failure details");
    failures += expect(
        initialize_failure.created.empty() &&
            initialize_failure.disabled.empty() &&
            initialize_failure.removed.empty(),
        "initialize failure creates and removes nothing");

    FakeState create_failure{};
    create_failure.create_status = MH_ERROR_NOT_EXECUTABLE;
    AudioHookFailure create_error{};
    failures += expect(
        !install(true, create_failure, &create_error),
        "create failure propagates");
    failures += expect_failure(
        create_error,
        AudioHookStage::CreateHook,
        MH_ERROR_NOT_EXECUTABLE,
        ERROR_SUCCESS,
        target,
        "create failure details");
    failures += expect(
        only_target(create_failure.created, target) &&
            create_failure.disabled.empty() && create_failure.removed.empty(),
        "create failure removes nothing not created");

    FakeState queue_failure{};
    queue_failure.queue_status = MH_ERROR_MEMORY_PROTECT;
    AudioHookFailure queue_error{};
    failures += expect(
        !install(true, queue_failure, &queue_error),
        "queue failure propagates");
    failures += expect_failure(
        queue_error,
        AudioHookStage::QueueEnable,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_SUCCESS,
        target,
        "queue failure details");
    failures += expect(
        only_target(queue_failure.disabled, target) &&
            only_target(queue_failure.removed, target),
        "queue failure rolls back exact created target");
    failures += expect(
        never_all_hooks(queue_failure),
        "queue rollback never uses MH_ALL_HOOKS");

    FakeState apply_failure{};
    apply_failure.apply_status = MH_ERROR_MEMORY_PROTECT;
    AudioHookFailure apply_error{};
    failures += expect(
        !install(true, apply_failure, &apply_error),
        "apply failure propagates");
    failures += expect_failure(
        apply_error,
        AudioHookStage::ApplyQueued,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_SUCCESS,
        target,
        "apply failure details");
    failures += expect(
        only_target(apply_failure.disabled, target) &&
            only_target(apply_failure.removed, target),
        "apply failure rolls back exact created target");
    failures += expect(
        never_all_hooks(apply_failure),
        "apply rollback never uses MH_ALL_HOOKS");
    failures += expect(
        apply_failure.engine_factory_calls == 0,
        "failure paths perform no engine calls");

    return failures == 0 ? 0 : 1;
}
