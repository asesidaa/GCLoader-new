#include "WasapiAudioPatch.h"
#include "WasapiAudioPatchInternal.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

using gc::audio::AudioHookFailure;
using gc::audio::AudioHookStage;

constexpr gc::audio::AudioMinHookApi kDefaultMinHookApi;
static_assert(kDefaultMinHookApi.initialize == nullptr);
static_assert(kDefaultMinHookApi.create == nullptr);
static_assert(kDefaultMinHookApi.queue_enable == nullptr);
static_assert(kDefaultMinHookApi.apply == nullptr);
static_assert(kDefaultMinHookApi.disable == nullptr);
static_assert(kDefaultMinHookApi.remove == nullptr);

constexpr gc::audio::detail::AudioResolverApi kDefaultResolverApi;
static_assert(kDefaultResolverApi.get_module_handle == nullptr);
static_assert(kDefaultResolverApi.get_proc_address == nullptr);

struct FakeState {
    HMODULE module{reinterpret_cast<HMODULE>(0x1000)};
    FARPROC export_address{reinterpret_cast<FARPROC>(0x2000)};
    MH_STATUS initialize_status{MH_OK};
    MH_STATUS create_status{MH_OK};
    MH_STATUS queue_status{MH_OK};
    MH_STATUS apply_status{MH_OK};
    MH_STATUS disable_status{MH_OK};
    MH_STATUS remove_status{MH_OK};
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
    std::vector<std::string> calls;
    LPVOID detour{};
    LPVOID* original_storage{};
};

FakeState* g_fake{};

HMODULE WINAPI fake_get_module_handle(LPCWSTR module_name) {
    g_fake->calls.emplace_back("resolve-module");
    ++g_fake->module_calls;
    g_fake->module_name = module_name == nullptr ? L"" : module_name;
    return g_fake->module;
}

FARPROC WINAPI fake_get_proc_address(HMODULE module, LPCSTR export_name) {
    g_fake->calls.emplace_back("resolve-export");
    ++g_fake->export_calls;
    if (module != g_fake->module) {
        return nullptr;
    }
    g_fake->export_name = export_name == nullptr ? "" : export_name;
    return g_fake->export_address;
}

MH_STATUS WINAPI fake_initialize() {
    g_fake->calls.emplace_back("initialize");
    ++g_fake->initialize_calls;
    return g_fake->initialize_status;
}

MH_STATUS WINAPI fake_create(LPVOID target, LPVOID detour, LPVOID* original) {
    g_fake->calls.emplace_back("create");
    g_fake->created.push_back(target);
    g_fake->detour = detour;
    g_fake->original_storage = original;
    if (g_fake->create_status == MH_OK && original != nullptr) {
        *original = reinterpret_cast<LPVOID>(0x3000);
    }
    return g_fake->create_status;
}

MH_STATUS WINAPI fake_queue_enable(LPVOID target) {
    g_fake->calls.emplace_back("queue");
    g_fake->queued.push_back(target);
    return g_fake->queue_status;
}

MH_STATUS WINAPI fake_apply_queued() {
    g_fake->calls.emplace_back("apply");
    ++g_fake->apply_calls;
    return g_fake->apply_status;
}

MH_STATUS WINAPI fake_disable(LPVOID target) {
    g_fake->calls.emplace_back("disable");
    g_fake->disabled.push_back(target);
    return g_fake->disable_status;
}

MH_STATUS WINAPI fake_remove(LPVOID target) {
    g_fake->calls.emplace_back("remove");
    g_fake->removed.push_back(target);
    return g_fake->remove_status;
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

bool install_with_apis(
    bool enabled,
    FakeState& state,
    gc::audio::AudioMinHookApi minhook,
    gc::audio::detail::AudioResolverApi resolver,
    AudioHookFailure* failure) {
    g_fake = &state;
    return gc::audio::detail::InstallWasapiAudioHookWithResolver(
        enabled,
        minhook,
        resolver,
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

int expect_rollback(
    const AudioHookFailure& actual,
    bool attempted,
    MH_STATUS disable_status,
    MH_STATUS remove_status,
    bool complete,
    const char* name) {
    int failures = 0;
    failures += expect(actual.rollback_attempted == attempted, name);
    failures += expect(
        actual.rollback_disable_status == disable_status,
        name);
    failures += expect(actual.rollback_remove_status == remove_status, name);
    failures += expect(actual.rollback_complete == complete, name);
    return failures;
}

int expect_no_calls(const FakeState& state, const char* name) {
    return expect(
        state.calls.empty() && state.module_calls == 0 &&
            state.export_calls == 0 && state.initialize_calls == 0 &&
            state.created.empty() && state.queued.empty() &&
            state.apply_calls == 0 && state.disabled.empty() &&
            state.removed.empty(),
        name);
}

int exercise_rollback_statuses(
    bool queue_origin,
    MH_STATUS disable_status,
    MH_STATUS remove_status,
    bool expected_complete,
    const char* name) {
    const auto target = reinterpret_cast<LPVOID>(0x2000);
    FakeState state{};
    state.queue_status =
        queue_origin ? MH_ERROR_MEMORY_PROTECT : MH_OK;
    state.apply_status =
        queue_origin ? MH_OK : MH_ERROR_MEMORY_PROTECT;
    state.disable_status = disable_status;
    state.remove_status = remove_status;

    AudioHookFailure failure{};
    int failures = 0;
    failures += expect(!install(true, state, &failure), name);
    failures += expect_failure(
        failure,
        queue_origin ? AudioHookStage::QueueEnable
                     : AudioHookStage::ApplyQueued,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_SUCCESS,
        target,
        name);
    failures += expect_rollback(
        failure,
        true,
        disable_status,
        remove_status,
        expected_complete,
        name);
    failures += expect(
        only_target(state.disabled, target) &&
            only_target(state.removed, target),
        name);
    failures += expect(
        state.calls.size() >= 2 &&
            state.calls[state.calls.size() - 2] == "disable" &&
            state.calls.back() == "remove",
        name);
    return failures;
}

int expect_invalid_api_rejected(
    gc::audio::AudioMinHookApi minhook,
    gc::audio::detail::AudioResolverApi resolver,
    const char* name) {
    FakeState state{};
    AudioHookFailure failure{};
    int failures = 0;
    failures += expect(
        !install_with_apis(true, state, minhook, resolver, &failure),
        name);
    failures += expect_failure(
        failure,
        AudioHookStage::ValidateApi,
        MH_UNKNOWN,
        ERROR_INVALID_PARAMETER,
        nullptr,
        name);
    failures += expect_rollback(
        failure,
        false,
        MH_OK,
        MH_OK,
        true,
        name);
    failures += expect_no_calls(state, name);
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    const auto target = reinterpret_cast<LPVOID>(0x2000);

    FakeState disabled_null_failure{};
    failures += expect(
        install_with_apis(
            false,
            disabled_null_failure,
            {},
            {},
            nullptr),
        "disabled mode accepts null failure output");
    failures += expect_no_calls(
        disabled_null_failure,
        "disabled null failure performs zero validation and calls");

    FakeState enabled_null_failure{};
    failures += expect(
        !install_with_apis(
            true,
            enabled_null_failure,
            fake_minhook_api(),
            fake_resolver_api(),
            nullptr),
        "enabled mode rejects null failure output");
    failures += expect_no_calls(
        enabled_null_failure,
        "enabled null failure performs zero validation and calls");

    FakeState disabled_incomplete{};
    AudioHookFailure disabled_incomplete_failure{
        AudioHookStage::ApplyQueued,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_ACCESS_DENIED,
        target,
    };
    failures += expect(
        install_with_apis(
            false,
            disabled_incomplete,
            {},
            {},
            &disabled_incomplete_failure),
        "disabled mode bypasses incomplete table validation");
    failures += expect_no_calls(
        disabled_incomplete,
        "disabled incomplete tables perform zero calls");
    failures += expect_failure(
        disabled_incomplete_failure,
        AudioHookStage::None,
        MH_OK,
        ERROR_SUCCESS,
        nullptr,
        "disabled incomplete tables clear failure");
    failures += expect_rollback(
        disabled_incomplete_failure,
        false,
        MH_OK,
        MH_OK,
        true,
        "disabled incomplete tables need no rollback");

    failures += expect_invalid_api_rejected(
        fake_minhook_api(),
        {},
        "empty resolver table rejected before calls");

    auto missing_initialize = fake_minhook_api();
    missing_initialize.initialize = nullptr;
    failures += expect_invalid_api_rejected(
        missing_initialize,
        fake_resolver_api(),
        "missing initialize rejected before calls");

    auto missing_create = fake_minhook_api();
    missing_create.create = nullptr;
    failures += expect_invalid_api_rejected(
        missing_create,
        fake_resolver_api(),
        "missing create rejected before calls");

    auto missing_queue = fake_minhook_api();
    missing_queue.queue_enable = nullptr;
    failures += expect_invalid_api_rejected(
        missing_queue,
        fake_resolver_api(),
        "missing queue rejected before calls");

    auto missing_apply = fake_minhook_api();
    missing_apply.apply = nullptr;
    failures += expect_invalid_api_rejected(
        missing_apply,
        fake_resolver_api(),
        "missing apply rejected before calls");

    auto missing_disable = fake_minhook_api();
    missing_disable.disable = nullptr;
    failures += expect_invalid_api_rejected(
        missing_disable,
        fake_resolver_api(),
        "missing rollback disable rejected before create");

    auto missing_remove = fake_minhook_api();
    missing_remove.remove = nullptr;
    failures += expect_invalid_api_rejected(
        missing_remove,
        fake_resolver_api(),
        "missing rollback remove rejected before create");

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
    failures += expect_rollback(
        disabled_failure,
        false,
        MH_OK,
        MH_OK,
        true,
        "disabled mode needs no rollback");

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
    failures += expect_rollback(
        success_failure,
        false,
        MH_OK,
        MH_OK,
        true,
        "successful install needs no rollback");

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
    failures += expect_rollback(
        queue_error,
        true,
        MH_OK,
        MH_OK,
        true,
        "queue rollback records clean statuses");
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
    failures += expect_rollback(
        apply_error,
        true,
        MH_OK,
        MH_OK,
        true,
        "apply rollback records clean statuses");
    failures += expect(
        never_all_hooks(apply_failure),
        "apply rollback never uses MH_ALL_HOOKS");
    failures += expect(
        apply_failure.engine_factory_calls == 0,
        "failure paths perform no engine calls");

    failures += exercise_rollback_statuses(
        true,
        MH_ERROR_MEMORY_PROTECT,
        MH_OK,
        true,
        "queue: disable failure and remove success is clean");
    failures += exercise_rollback_statuses(
        true,
        MH_OK,
        MH_ERROR_MEMORY_PROTECT,
        false,
        "queue: remove failure is incomplete");
    failures += exercise_rollback_statuses(
        true,
        MH_ERROR_MEMORY_PROTECT,
        MH_ERROR_MEMORY_PROTECT,
        false,
        "queue: both cleanup calls failing is incomplete");
    failures += exercise_rollback_statuses(
        false,
        MH_ERROR_MEMORY_PROTECT,
        MH_ERROR_NOT_CREATED,
        true,
        "apply: remove not-created is clean despite disable failure");
    failures += exercise_rollback_statuses(
        false,
        MH_OK,
        MH_ERROR_MEMORY_PROTECT,
        false,
        "apply: remove failure is incomplete");
    failures += exercise_rollback_statuses(
        false,
        MH_ERROR_MEMORY_PROTECT,
        MH_ERROR_MEMORY_PROTECT,
        false,
        "apply: both cleanup calls failing is incomplete");

    return failures == 0 ? 0 : 1;
}
