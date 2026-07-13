#include "WasapiAudioPatch.h"

#include "DirectSoundFacade.h"
#include "ExclusiveAudioEngine.h"
#include "WasapiAudioPatchInternal.h"
#include "config.h"

#include "plog/Log.h"

#include <dsound.h>

#include <cstdlib>
#include <exception>
#include <iomanip>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gc::audio {
namespace {

LPVOID g_original_direct_sound_create8{};
LPVOID g_committed_target{};

static_assert(std::is_nothrow_move_constructible_v<AudioStartupFailure>);
static_assert(std::is_nothrow_move_assignable_v<AudioStartupFailure>);

constexpr std::string_view kFailureMessage =
    "WASAPI exclusive low-latency audio failed.\n"
    "Restart the game after setting enable_wasapi_exclusive_audio = false\n"
    "to restore the original DirectSound backend.";
constexpr std::string_view kExactFormat =
    "pcm16/44100Hz/2ch/16bit";
constexpr REFERENCE_TIME kReferenceTimePerMillisecond = 10'000;

constexpr REFERENCE_TIME BufferMillisecondsToReferenceTime(
    std::uint32_t milliseconds) noexcept {
    return static_cast<REFERENCE_TIME>(milliseconds) *
        kReferenceTimePerMillisecond;
}

const char* audio_failure_stage_name(AudioFailureStage stage) noexcept {
    switch (stage) {
    case AudioFailureStage::None: return "None";
    case AudioFailureStage::InitializationTimeout: return "InitializationTimeout";
    case AudioFailureStage::InitializeMixer: return "InitializeMixer";
    case AudioFailureStage::CoInitialize: return "CoInitialize";
    case AudioFailureStage::OpenDefaultEndpoint: return "OpenDefaultEndpoint";
    case AudioFailureStage::ActivateAudioClient: return "ActivateAudioClient";
    case AudioFailureStage::IsFormatSupported: return "IsFormatSupported";
    case AudioFailureStage::GetDevicePeriod: return "GetDevicePeriod";
    case AudioFailureStage::InitializeExclusive: return "InitializeExclusive";
    case AudioFailureStage::GetAlignedBufferSize: return "GetAlignedBufferSize";
    case AudioFailureStage::ReactivateAudioClient: return "ReactivateAudioClient";
    case AudioFailureStage::RetryInitializeExclusive: return "RetryInitializeExclusive";
    case AudioFailureStage::GetActualBufferSize: return "GetActualBufferSize";
    case AudioFailureStage::CreateRenderEvent: return "CreateRenderEvent";
    case AudioFailureStage::SetEventHandle: return "SetEventHandle";
    case AudioFailureStage::GetRenderService: return "GetRenderService";
    case AudioFailureStage::GetClockService: return "GetClockService";
    case AudioFailureStage::GetClockFrequency: return "GetClockFrequency";
    case AudioFailureStage::PrefillGetBuffer: return "PrefillGetBuffer";
    case AudioFailureStage::PrefillReleaseBuffer: return "PrefillReleaseBuffer";
    case AudioFailureStage::RegisterMmcss: return "RegisterMmcss";
    case AudioFailureStage::SetMmcssPriority: return "SetMmcssPriority";
    case AudioFailureStage::StartEndpoint: return "StartEndpoint";
    case AudioFailureStage::WaitRenderEvent: return "WaitRenderEvent";
    case AudioFailureStage::GetRenderBuffer: return "GetRenderBuffer";
    case AudioFailureStage::ReleaseRenderBuffer: return "ReleaseRenderBuffer";
    case AudioFailureStage::GetClockPosition: return "GetClockPosition";
    }
    return "Unknown";
}

const char* audio_hook_stage_name(AudioHookStage stage) noexcept {
    switch (stage) {
    case AudioHookStage::None: return "None";
    case AudioHookStage::ValidateApi: return "ValidateApi";
    case AudioHookStage::ResolveModule: return "ResolveModule";
    case AudioHookStage::ResolveExport: return "ResolveExport";
    case AudioHookStage::InitializeMinHook: return "InitializeMinHook";
    case AudioHookStage::CreateHook: return "CreateHook";
    case AudioHookStage::QueueEnable: return "QueueEnable";
    case AudioHookStage::ApplyQueued: return "ApplyQueued";
    }
    return "Unknown";
}

std::string utf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return "<conversion-failed>";
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            result.data(), size, nullptr, nullptr) != size) {
        return "<conversion-failed>";
    }
    return result;
}

std::string counters_text(const AudioRuntimeCountersSnapshot& counters) {
    std::ostringstream stream;
    stream
        << "render_callbacks=" << counters.render_callbacks
        << " late_event_wakes=" << counters.late_event_wakes
        << " silence_fallbacks=" << counters.silence_fallbacks
        << " cursor_timeline_failures=" << counters.cursor_timeline_failures
        << " endpoint_hresult_failures=" << counters.endpoint_hresult_failures
        << " native_rate_buffers=" << counters.mixer.native_rate_buffers
        << " sample_format_converted_buffers="
        << counters.mixer.sample_format_converted_buffers
        << " sample_rate_converted_buffers="
        << counters.mixer.sample_rate_converted_buffers
        << " native_gameplay_buffers="
        << counters.mixer.native_gameplay_buffers
        << " active_voices=" << counters.mixer.active_voices
        << " maximum_simultaneous_voices="
        << counters.mixer.maximum_simultaneous_voices;
    return stream.str();
}

std::string hresult_hex(HRESULT result) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setfill('0')
           << std::setw(8) << static_cast<std::uint32_t>(result);
    return stream.str();
}

std::string startup_text(const EndpointInitialization& initialization) {
    const double actual_ms =
        static_cast<double>(initialization.actual_buffer_frames) * 1000.0 /
        static_cast<double>(kOutputSampleRate);
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3)
        << "WASAPI audio startup requested_backend=wasapi_exclusive"
        << " active_backend=wasapi_exclusive"
        << " endpoint_name=\"" << utf8(initialization.endpoint_name) << "\""
        << " endpoint_id=\"" << utf8(initialization.endpoint_id) << "\""
        << " format=" << kExactFormat
        << " default_period_100ns=" << initialization.default_period
        << " default_period_ms="
        << static_cast<double>(initialization.default_period) / 10'000.0
        << " minimum_period_100ns=" << initialization.minimum_period
        << " minimum_period_ms="
        << static_cast<double>(initialization.minimum_period) / 10'000.0
        << " configured_duration_100ns="
        << initialization.configured_duration
        << " configured_duration_ms="
        << static_cast<double>(initialization.configured_duration) / 10'000.0
        << " requested_duration_100ns=" << initialization.requested_duration
        << " requested_duration_ms="
        << static_cast<double>(initialization.requested_duration) / 10'000.0
        << " actual_buffer_frames=" << initialization.actual_buffer_frames
        << " actual_buffer_ms=" << actual_ms
        << " exclusive_event_driven=true"
        << " alignment_retry="
        << (initialization.alignment_retry ? "true" : "false")
        << " mmcss_profile=\"Pro Audio\""
        << " mmcss_priority=\"Critical\""
        << " mixer_rate_hz=" << kOutputSampleRate
        << " mixer_channels=" << kOutputChannels;
    return stream.str();
}

std::string failure_text(
    std::string_view kind,
    const EndpointInitialization& initialization,
    const AudioFailure& failure) {
    std::ostringstream stream;
    stream << kind
        << " endpoint_id=\""
        << (initialization.endpoint_id.empty()
                ? "<unknown>"
                : utf8(initialization.endpoint_id))
        << "\" stage=" << audio_failure_stage_name(failure.stage)
        << " hresult=" << hresult_hex(failure.result)
        << " format=" << kExactFormat;
    return stream.str();
}

std::string hook_failure_text(const AudioHookFailure& failure) {
    std::ostringstream stream;
    stream << "WasapiAudioPatch: hook install failed"
        << " stage=" << audio_hook_stage_name(failure.stage)
        << " status=" << static_cast<int>(failure.status)
        << " win32_error=" << failure.win32_error
        << " target=0x" << std::uppercase << std::hex << std::setfill('0')
        << std::setw(sizeof(std::uintptr_t) * 2)
        << reinterpret_cast<std::uintptr_t>(failure.target)
        << std::dec
        << " rollback_attempted="
        << (failure.rollback_attempted ? "true" : "false")
        << " rollback_disable_status="
        << static_cast<int>(failure.rollback_disable_status)
        << " rollback_remove_status="
        << static_cast<int>(failure.rollback_remove_status)
        << " rollback_complete="
        << (failure.rollback_complete ? "true" : "false");
    return stream.str();
}

void emit_info(
    detail::AudioPatchPlatformActions actions,
    const char* text) noexcept {
    if (actions.log_info != nullptr) {
        try { actions.log_info(text); } catch (...) {}
    }
}

void emit_error(
    detail::AudioPatchPlatformActions actions,
    const char* text) noexcept {
    if (actions.log_error != nullptr) {
        try { actions.log_error(text); } catch (...) {}
    }
}

void show_error(detail::AudioPatchPlatformActions actions) noexcept {
    if (actions.show_error != nullptr) {
        try { actions.show_error(kFailureMessage.data()); } catch (...) {}
    }
}

void production_log_info(const char* text) {
    PLOG_INFO << (text == nullptr ? "" : text);
}

void production_log_error(const char* text) {
    PLOG_ERROR << (text == nullptr ? "" : text);
}

void production_show_error(const char* text) {
    MessageBoxA(
        nullptr,
        text == nullptr ? "" : text,
        "GCLoader WASAPI audio error",
        MB_OK | MB_ICONERROR);
}

void production_terminate_process(DWORD exit_code) {
    TerminateProcess(GetCurrentProcess(), exit_code);
}

[[noreturn]] void production_fail_fast() {
    RaiseFailFastException(nullptr, nullptr, 0);
    std::abort();
}

detail::AudioPatchPlatformActions production_platform_actions() noexcept {
    return {
        production_log_info,
        production_log_error,
        production_show_error,
        production_terminate_process,
        production_fail_fast,
    };
}

class ProductionAudioObserver final : public IAudioEngineObserver {
public:
    explicit ProductionAudioObserver(
        detail::AudioPatchPlatformActions actions) noexcept
        : actions_(actions) {}

    void StartupSucceeded(
        const EndpointInitialization& initialization) noexcept override {
        initialization_ = &initialization;
        detail::ReportAudioStartupSucceeded(initialization, actions_);
    }
    void RuntimeSummary(
        const AudioRuntimeCountersSnapshot& counters) noexcept override {
        detail::ReportAudioRuntimeSummary(counters, actions_);
    }
    void RuntimeFailed(
        const AudioFailure& failure,
        const AudioRuntimeCountersSnapshot& counters) noexcept override {
        const EndpointInitialization unknown{};
        detail::ReportAudioRuntimeFailure(
            initialization_ == nullptr ? unknown : *initialization_,
            failure,
            counters,
            actions_);
    }

private:
    detail::AudioPatchPlatformActions actions_{};
    const EndpointInitialization* initialization_{};
};

class ProductionExclusiveEngineStartup final
    : public detail::IExclusiveEngineStartup {
public:
    explicit ProductionExclusiveEngineStartup(
        detail::AudioPatchPlatformActions actions,
        REFERENCE_TIME configured_duration) noexcept
        : actions_(actions),
          configured_duration_(configured_duration) {}

    IAudioEngineServices* Start(
        AudioStartupFailure* startup_failure) noexcept override {
        std::shared_ptr<ProductionAudioObserver> observer;
        try {
            observer = std::make_shared<ProductionAudioObserver>(actions_);
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

        auto engine = detail::StartProductionExclusiveAudioEngine(
            CreateProductionWasapiApi,
            &ExclusiveAudioEngine::StartAndWait,
            configured_duration_,
            std::move(observer),
            startup_failure);
        if (engine == nullptr) {
            return nullptr;
        }
        engine_ = std::move(engine);
        return engine_.get();
    }

private:
    detail::AudioPatchPlatformActions actions_{};
    REFERENCE_TIME configured_duration_{};
    std::unique_ptr<ExclusiveAudioEngine> engine_;
};

class ProductionAudioStartupFailureReporter final
    : public IAudioStartupFailureReporter {
public:
    explicit ProductionAudioStartupFailureReporter(
        detail::AudioPatchPlatformActions actions) noexcept
        : actions_(actions) {}

    void FatalStartupFailure(
        const AudioStartupFailure& failure) noexcept override {
        detail::ReportAudioStartupFailure(failure, actions_);
    }

private:
    detail::AudioPatchPlatformActions actions_{};
};

struct ProductionDetourState {
    // This process-lifetime state samples the parsed value exactly once.
    ProductionDetourState()
        : startup(
              production_platform_actions(),
              BufferMillisecondsToReferenceTime(
                  ConfigManager::instance().GetWasapiExclusiveBufferMs())) {}

    ProductionExclusiveEngineStartup startup;
    detail::CachedExclusiveEngineFactory factory{startup};
};

ProductionAudioStartupFailureReporter g_startup_failure_reporter{
    production_platform_actions()};

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

void ReportAudioStartupSucceeded(
    const EndpointInitialization& initialization,
    AudioPatchPlatformActions actions) noexcept {
    try {
        const auto text = startup_text(initialization);
        emit_info(actions, text.c_str());
    } catch (...) {
        emit_error(
            actions,
            "WASAPI audio startup diagnostics formatting failed");
    }
}

void ReportAudioRuntimeSummary(
    const AudioRuntimeCountersSnapshot& counters,
    AudioPatchPlatformActions actions) noexcept {
    try {
        const auto text = std::string{"WASAPI audio runtime summary "} +
            counters_text(counters);
        emit_info(actions, text.c_str());
    } catch (...) {
        emit_error(
            actions,
            "WASAPI audio runtime summary formatting failed");
    }
}

void ReportAudioRuntimeFailure(
    const EndpointInitialization& initialization,
    const AudioFailure& failure,
    const AudioRuntimeCountersSnapshot& counters,
    AudioPatchPlatformActions actions) noexcept {
    try {
        const auto text = failure_text(
            "WASAPI audio runtime fatal",
            initialization,
            failure) + " " + counters_text(counters);
        emit_error(actions, text.c_str());
    } catch (...) {
        emit_error(actions, "WASAPI audio runtime fatal formatting failed");
    }
    show_error(actions);
    if (actions.terminate_process != nullptr) {
        try {
            actions.terminate_process(ERROR_DEVICE_NOT_AVAILABLE);
        } catch (...) {}
    }
    if (actions.fail_fast != nullptr) {
        try { actions.fail_fast(); } catch (...) {}
    }
}

void ReportAudioStartupFailure(
    const AudioStartupFailure& failure,
    AudioPatchPlatformActions actions) noexcept {
    try {
        const auto text = failure_text(
            "WASAPI audio startup fatal",
            failure.attempted,
            failure.failure);
        emit_error(actions, text.c_str());
    } catch (...) {
        emit_error(actions, "WASAPI audio startup fatal formatting failed");
    }
    show_error(actions);
    if (actions.terminate_process != nullptr) {
        try {
            actions.terminate_process(ERROR_DEVICE_NOT_AVAILABLE);
        } catch (...) {}
    }
    if (actions.fail_fast != nullptr) {
        try { actions.fail_fast(); } catch (...) {}
    }
}

std::unique_ptr<ExclusiveAudioEngine> StartProductionExclusiveAudioEngine(
    CreateWasapiApiFn create_api,
    StartExclusiveAudioEngineFn start_engine,
    REFERENCE_TIME configured_duration,
    std::shared_ptr<IAudioEngineObserver> observer,
    AudioStartupFailure* startup_failure) noexcept {
    if (startup_failure != nullptr) {
        *startup_failure = {};
    }
    if (create_api == nullptr || start_engine == nullptr) {
        if (startup_failure != nullptr) {
            startup_failure->failure = {
                AudioFailureStage::InitializeMixer,
                E_INVALIDARG,
            };
        }
        return nullptr;
    }

    auto api = create_api();
    if (api == nullptr) {
        if (startup_failure != nullptr) {
            startup_failure->failure = {
                AudioFailureStage::InitializeMixer,
                E_OUTOFMEMORY,
            };
        }
        return nullptr;
    }

    return start_engine(
        std::move(api),
        std::move(observer),
        10'000,
        configured_duration,
        std::shared_ptr<const ma_allocation_callbacks>{},
        startup_failure);
}

bool WasapiAudioPatchInitWithDependencies(
    bool enabled,
    AudioPatchInitDependencies dependencies) {
    AudioHookFailure failure{};
    if (!InstallWasapiAudioHookWithResolver(
            enabled,
            dependencies.minhook,
            dependencies.resolver,
            enabled ? &failure : nullptr)) {
        try {
            const auto text = hook_failure_text(failure);
            emit_error(dependencies.platform, text.c_str());
        } catch (...) {
            emit_error(
                dependencies.platform,
                "WasapiAudioPatch: hook failure formatting failed");
        }
        show_error(dependencies.platform);
        if (failure.rollback_complete) {
            return false;
        }

        if (dependencies.platform.terminate_process != nullptr) {
            dependencies.platform.terminate_process(ERROR_DLL_INIT_FAILED);
        }
        if (dependencies.platform.fail_fast != nullptr) {
            dependencies.platform.fail_fast();
        }
        std::abort();
    }

    if (!enabled) {
        emit_info(
            dependencies.platform,
            "WASAPI audio config requested_backend=directsound "
            "active_backend=directsound hook_installed=false enabled=false");
    } else {
        emit_info(
            dependencies.platform,
            "WASAPI audio config requested_backend=wasapi_exclusive "
            "active_backend=wasapi_exclusive hook_installed=true");
    }
    return true;
}

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
    const auto actions = production_platform_actions();
    try {
        return detail::WasapiAudioPatchInitWithDependencies(
            ConfigManager::instance().GetEnableWasapiExclusiveAudio(),
            {
                {
                    MH_Initialize,
                    MH_CreateHook,
                    MH_QueueEnableHook,
                    MH_ApplyQueued,
                    MH_DisableHook,
                    MH_RemoveHook,
                },
                {GetModuleHandleW, GetProcAddress},
                actions,
            });
    } catch (...) {
        emit_error(
            actions,
            "WasapiAudioPatch: unexpected attach initialization failure");
        show_error(actions);
        actions.terminate_process(ERROR_DLL_INIT_FAILED);
        actions.fail_fast();
        std::abort();
    }
}

} // namespace gc::audio
