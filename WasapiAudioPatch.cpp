#include "WasapiAudioPatch.h"

#include "DirectSoundFacade.h"
#include "ExclusiveAudioEngine.h"
#include "WasapiAudioPatchInternal.h"

#include <dsound.h>

#include <exception>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace gc::audio {
namespace {

LPVOID g_original_direct_sound_create8{};
LPVOID g_committed_target{};

static_assert(std::is_nothrow_move_constructible_v<AudioStartupFailure>);
static_assert(std::is_nothrow_move_assignable_v<AudioStartupFailure>);

class ProductionAudioObserver final : public IAudioEngineObserver {
public:
    void StartupSucceeded(const EndpointInitialization&) noexcept override {}
    void RuntimeSummary(
        const AudioRuntimeCountersSnapshot&) noexcept override {}
    void RuntimeFailed(
        const AudioFailure&,
        const AudioRuntimeCountersSnapshot&) noexcept override {
        std::terminate();
    }
};

class ProductionExclusiveEngineStartup final
    : public detail::IExclusiveEngineStartup {
public:
    IAudioEngineServices* Start(
        AudioStartupFailure* startup_failure) noexcept override {
        std::shared_ptr<ProductionAudioObserver> observer;
        try {
            observer = std::make_shared<ProductionAudioObserver>();
        } catch (...) {
            if (startup_failure != nullptr) {
                *startup_failure = {};
                startup_failure->failure = {
                    AudioFailureStage::InitializeMixer,
                    E_OUTOFMEMORY,
                };
            }
            return nullptr;
        }

        auto engine = ExclusiveAudioEngine::StartAndWait(
            CreateProductionWasapiApi(),
            std::move(observer),
            10'000,
            std::shared_ptr<const ma_allocation_callbacks>{},
            startup_failure);
        if (engine == nullptr) {
            return nullptr;
        }
        engine_ = std::move(engine);
        return engine_.get();
    }

private:
    std::unique_ptr<ExclusiveAudioEngine> engine_;
};

class ProductionAudioStartupFailureReporter final
    : public IAudioStartupFailureReporter {
public:
    void FatalStartupFailure(
        const AudioStartupFailure&) noexcept override {
        // Task 3 replaces this fail-closed placeholder with actionable
        // diagnostics and the production TerminateProcess path.
        std::terminate();
    }
};

struct ProductionDetourState {
    ProductionExclusiveEngineStartup startup;
    detail::CachedExclusiveEngineFactory factory{startup};
};

ProductionAudioStartupFailureReporter g_startup_failure_reporter;

ProductionDetourState* production_detour_state() noexcept {
    // Deliberately process-lifetime: DirectSound facade release and process
    // detach must not tear down the engine or join its endpoint threads.
    static ProductionDetourState* state = []() noexcept {
        try {
            return new (std::nothrow) ProductionDetourState();
        } catch (...) {
            return static_cast<ProductionDetourState*>(nullptr);
        }
    }();
    return state;
}

struct RollbackResult {
    MH_STATUS disable_status{MH_OK};
    MH_STATUS remove_status{MH_OK};
    bool complete{true};
};

HRESULT WINAPI DirectSoundCreate8Detour(
    LPCGUID device_guid,
    LPDIRECTSOUND8* output,
    LPUNKNOWN outer) {
    if (output == nullptr) {
        return DSERR_INVALIDPARAM;
    }
    *output = nullptr;
    if (device_guid != nullptr) {
        return DSERR_NODRIVER;
    }
    if (outer != nullptr) {
        return DSERR_NOAGGREGATION;
    }

    auto* state = production_detour_state();
    if (state == nullptr) {
        AudioStartupFailure failure{};
        failure.failure = {
            AudioFailureStage::InitializeMixer,
            E_OUTOFMEMORY,
        };
        g_startup_failure_reporter.FatalStartupFailure(failure);
        return DSERR_NODRIVER;
    }
    return detail::InvokeDirectSoundCreate8Detour(
        device_guid,
        output,
        outer,
        state->factory,
        g_startup_failure_reporter,
        reinterpret_cast<DirectSoundCreate8Fn>(
            g_original_direct_sound_create8));
}

void set_failure(
    AudioHookFailure* failure,
    AudioHookStage stage,
    MH_STATUS status,
    DWORD win32_error,
    LPVOID target) noexcept {
    if (failure != nullptr) {
        *failure = {stage, status, win32_error, target};
    }
}

RollbackResult rollback(AudioMinHookApi api, LPVOID target) noexcept {
    RollbackResult result{};
    result.disable_status = api.disable(target);
    result.remove_status = api.remove(target);
    result.complete = result.remove_status == MH_OK ||
        result.remove_status == MH_ERROR_NOT_CREATED;
    return result;
}

void record_rollback(
    AudioHookFailure* failure,
    RollbackResult rollback_result) noexcept {
    if (failure != nullptr) {
        failure->rollback_attempted = true;
        failure->rollback_disable_status = rollback_result.disable_status;
        failure->rollback_remove_status = rollback_result.remove_status;
        failure->rollback_complete = rollback_result.complete;
    }
}

bool complete_api_tables(
    AudioMinHookApi minhook,
    detail::AudioResolverApi resolver) noexcept {
    return resolver.get_module_handle != nullptr &&
        resolver.get_proc_address != nullptr &&
        minhook.initialize != nullptr && minhook.create != nullptr &&
        minhook.queue_enable != nullptr && minhook.apply != nullptr &&
        minhook.disable != nullptr && minhook.remove != nullptr;
}

} // namespace

namespace detail {

CachedExclusiveEngineFactory::CachedExclusiveEngineFactory(
    IExclusiveEngineStartup& startup) noexcept
    : startup_(startup) {}

IAudioEngineServices* CachedExclusiveEngineFactory::GetOrCreate(
    const AudioStartupFailure** startup_failure) noexcept {
    if (startup_failure != nullptr) {
        *startup_failure = nullptr;
    }

    {
        std::unique_lock lock(mutex_);
        while (state_ == State::Initializing) {
            condition_.wait(lock);
        }
        if (state_ == State::Succeeded) {
            return engine_;
        }
        if (state_ == State::Failed) {
            if (startup_failure != nullptr) {
                *startup_failure = &failure_;
            }
            return nullptr;
        }
        state_ = State::Initializing;
    }

    AudioStartupFailure observed_failure{};
    auto* observed_engine = startup_.Start(&observed_failure);

    {
        std::lock_guard lock(mutex_);
        if (observed_engine != nullptr) {
            engine_ = observed_engine;
            state_ = State::Succeeded;
        } else {
            failure_ = std::move(observed_failure);
            state_ = State::Failed;
            if (startup_failure != nullptr) {
                *startup_failure = &failure_;
            }
        }
    }
    condition_.notify_all();
    return observed_engine;
}

HRESULT InvokeDirectSoundCreate8Detour(
    LPCGUID device_guid,
    LPDIRECTSOUND8* output,
    LPUNKNOWN outer,
    IExclusiveEngineFactory& factory,
    IAudioStartupFailureReporter& reporter,
    DirectSoundCreate8Fn saved_original) noexcept {
    static_cast<void>(saved_original);
    if (output == nullptr) {
        return DSERR_INVALIDPARAM;
    }
    *output = nullptr;
    if (device_guid != nullptr) {
        return DSERR_NODRIVER;
    }
    if (outer != nullptr) {
        return DSERR_NOAGGREGATION;
    }

    const AudioStartupFailure* startup_failure{};
    auto* engine = factory.GetOrCreate(&startup_failure);
    if (engine == nullptr) {
        AudioStartupFailure missing_failure{};
        if (startup_failure == nullptr) {
            missing_failure.failure = {
                AudioFailureStage::InitializeMixer,
                E_UNEXPECTED,
            };
            startup_failure = &missing_failure;
        }
        reporter.FatalStartupFailure(*startup_failure);
        return DSERR_NODRIVER;
    }
    return CreateDirectSoundDevice(*engine, output);
}

bool InstallWasapiAudioHookWithResolver(
    bool enabled,
    AudioMinHookApi minhook,
    AudioResolverApi resolver,
    AudioHookFailure* failure) noexcept {
    if (!enabled) {
        if (failure != nullptr) {
            *failure = {};
        }
        return true;
    }
    if (failure == nullptr) {
        return false;
    }
    *failure = {};
    if (!complete_api_tables(minhook, resolver)) {
        set_failure(
            failure,
            AudioHookStage::ValidateApi,
            MH_UNKNOWN,
            ERROR_INVALID_PARAMETER,
            nullptr);
        return false;
    }

    const auto module = resolver.get_module_handle(L"dsound.dll");
    if (module == nullptr) {
        set_failure(
            failure,
            AudioHookStage::ResolveModule,
            MH_OK,
            ERROR_MOD_NOT_FOUND,
            nullptr);
        return false;
    }

    const auto target = reinterpret_cast<LPVOID>(
        resolver.get_proc_address(module, "DirectSoundCreate8"));
    if (target == nullptr) {
        set_failure(
            failure,
            AudioHookStage::ResolveExport,
            MH_OK,
            ERROR_PROC_NOT_FOUND,
            nullptr);
        return false;
    }

    auto status = minhook.initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        set_failure(
            failure,
            AudioHookStage::InitializeMinHook,
            status,
            ERROR_SUCCESS,
            target);
        return false;
    }

    status = minhook.create(
        target,
        reinterpret_cast<LPVOID>(&DirectSoundCreate8Detour),
        &g_original_direct_sound_create8);
    if (status != MH_OK) {
        set_failure(
            failure,
            AudioHookStage::CreateHook,
            status,
            ERROR_SUCCESS,
            target);
        return false;
    }

    status = minhook.queue_enable(target);
    if (status != MH_OK) {
        set_failure(
            failure,
            AudioHookStage::QueueEnable,
            status,
            ERROR_SUCCESS,
            target);
        record_rollback(failure, rollback(minhook, target));
        return false;
    }

    status = minhook.apply();
    if (status != MH_OK) {
        set_failure(
            failure,
            AudioHookStage::ApplyQueued,
            status,
            ERROR_SUCCESS,
            target);
        record_rollback(failure, rollback(minhook, target));
        return false;
    }

    g_committed_target = target;
    return true;
}

} // namespace detail

bool InstallWasapiAudioHook(
    bool enabled,
    AudioMinHookApi api,
    AudioHookFailure* failure) noexcept {
    return detail::InstallWasapiAudioHookWithResolver(
        enabled,
        api,
        {GetModuleHandleW, GetProcAddress},
        failure);
}

bool WasapiAudioPatchInit() noexcept {
    return true;
}

} // namespace gc::audio
