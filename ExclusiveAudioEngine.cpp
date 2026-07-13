#include "ExclusiveAudioEngine.h"

#include <algorithm>
#include <limits>
#include <new>

namespace gc::audio {

namespace {

#if defined(GC_EXCLUSIVE_AUDIO_ENGINE_SUMMARY_INTERVAL_MS)
constexpr DWORD kSummaryIntervalMs =
    GC_EXCLUSIVE_AUDIO_ENGINE_SUMMARY_INTERVAL_MS;
#else
constexpr DWORD kSummaryIntervalMs = 30'000;
#endif

constexpr DWORD kRenderWaitMs = 2'000;

HRESULT LastErrorResult() noexcept {
    const auto error = GetLastError();
    return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
}

} // namespace

ExclusiveAudioEngine::ExclusiveAudioEngine(
    std::unique_ptr<IWasapiApi> api,
    std::shared_ptr<IAudioEngineObserver> observer,
    const ma_allocation_callbacks* mixer_allocations) noexcept
    : pending_api_(std::move(api)),
      observer_(std::move(observer)),
      mixer_allocations_(mixer_allocations) {}

ExclusiveAudioEngine::~ExclusiveAudioEngine() {
    if (shutdown_event_ != nullptr) {
        SetEvent(shutdown_event_);
    }
    if (audio_thread_.joinable()) {
        if (audio_exited_event_ != nullptr) {
            WaitForSingleObject(audio_exited_event_, INFINITE);
        }
        audio_thread_.join();
    }
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    // Every finite post-create path resets the endpoint on the audio thread.
    // If a future regression violates that invariant, abandon rather than run
    // thread-affine endpoint destruction on this controlling thread.
    if (endpoint_ != nullptr) {
        static_cast<void>(endpoint_.release());
    }
    CloseControlEvents();
}

std::unique_ptr<ExclusiveAudioEngine> ExclusiveAudioEngine::StartAndWait(
    std::unique_ptr<IWasapiApi> api,
    std::shared_ptr<IAudioEngineObserver> observer,
    DWORD timeout_ms,
    const ma_allocation_callbacks* mixer_allocations,
    AudioStartupFailure* startup_failure) noexcept {
    if (startup_failure != nullptr) {
        *startup_failure = {};
    }
    if (api == nullptr || observer == nullptr) {
        if (startup_failure != nullptr) {
            startup_failure->failure = {
                AudioFailureStage::InitializeMixer, E_INVALIDARG};
        }
        return nullptr;
    }

    auto engine = std::unique_ptr<ExclusiveAudioEngine>(
        new (std::nothrow) ExclusiveAudioEngine(
            std::move(api), std::move(observer), mixer_allocations));
    if (engine == nullptr) {
        if (startup_failure != nullptr) {
            startup_failure->failure = {
                AudioFailureStage::InitializeMixer, E_OUTOFMEMORY};
        }
        return nullptr;
    }
    if (!engine->CreateControlEvents() || !engine->StartThreads()) {
        if (startup_failure != nullptr) {
            *startup_failure = engine->startup_failure_;
        }
        return nullptr;
    }

    const auto wait = WaitForSingleObject(
        engine->initialization_event_, timeout_ms);
    if (wait == WAIT_TIMEOUT) {
        if (startup_failure != nullptr) {
            startup_failure->failure = {
                AudioFailureStage::InitializationTimeout,
                HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        }
        // Initialization may be stuck inside COM or a driver. The enabled
        // caller reports a fatal startup error immediately, so blocking or
        // destroying this thread-affine object here is forbidden.
        static_cast<void>(engine.release());
        return nullptr;
    }
    if (wait != WAIT_OBJECT_0) {
        if (startup_failure != nullptr) {
            startup_failure->failure = {
                AudioFailureStage::InitializationTimeout,
                wait == WAIT_FAILED ? LastErrorResult() : E_UNEXPECTED};
        }
        static_cast<void>(engine.release());
        return nullptr;
    }

    if (!engine->initialization_succeeded_.load(std::memory_order_acquire)) {
        if (startup_failure != nullptr) {
            *startup_failure = engine->startup_failure_;
        }
        return nullptr;
    }

    engine->observer_->StartupSucceeded(engine->initialization_);
    return engine;
}

bool ExclusiveAudioEngine::CreateControlEvents() noexcept {
    initialization_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    fatal_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    shutdown_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    audio_exited_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (initialization_event_ != nullptr && fatal_event_ != nullptr &&
        shutdown_event_ != nullptr && audio_exited_event_ != nullptr) {
        return true;
    }
    startup_failure_.failure = {
        AudioFailureStage::InitializeMixer, LastErrorResult()};
    return false;
}

bool ExclusiveAudioEngine::StartThreads() noexcept {
    try {
        monitor_thread_ = std::thread([this] { MonitorThreadMain(); });
        audio_thread_ = std::thread([this] { AudioThreadMain(); });
        return true;
    } catch (const std::system_error&) {
        startup_failure_.failure = {
            AudioFailureStage::InitializeMixer, E_OUTOFMEMORY};
    } catch (const std::bad_alloc&) {
        startup_failure_.failure = {
            AudioFailureStage::InitializeMixer, E_OUTOFMEMORY};
    }
    if (initialization_event_ != nullptr) {
        SetEvent(initialization_event_);
    }
    if (shutdown_event_ != nullptr) {
        SetEvent(shutdown_event_);
    }
    return false;
}

void ExclusiveAudioEngine::AudioThreadMain() noexcept {
    AudioFailure failure{};
    EndpointInitialization attempted{};
    try {
        endpoint_ = WasapiEndpoint::Create(
            std::move(pending_api_), &attempted, &failure);
        if (endpoint_ == nullptr) {
            SignalInitializationFailure(failure, attempted);
            SetEvent(audio_exited_event_);
            return;
        }

        initialization_ = endpoint_->initialization();
        const auto frames = initialization_.actual_buffer_frames;
        endpoint_buffer_frames_.store(frames, std::memory_order_release);

        ma_result mixer_result = MA_ERROR;
        mixer_ = MiniaudioMixer::Create(
            frames, mixer_allocations_, &mixer_result);
        if (mixer_ == nullptr) {
            failure = {AudioFailureStage::InitializeMixer, E_OUTOFMEMORY};
            CleanupEndpointOnAudioThread();
            SignalInitializationFailure(failure, initialization_);
            SetEvent(audio_exited_event_);
            return;
        }

        float_mix_.resize(
            static_cast<std::size_t>(frames) * kOutputChannels);
        pcm16_mix_.resize(
            static_cast<std::size_t>(frames) * kOutputChannels);

        if (FAILED(endpoint_->Start(&failure))) {
            const auto failed_initialization = endpoint_->initialization();
            CleanupEndpointOnAudioThread();
            SignalInitializationFailure(failure, failed_initialization);
            SetEvent(audio_exited_event_);
            return;
        }

        EndpointClockPosition initial_clock{};
        if (FAILED(endpoint_->ReadClock(&initial_clock, &failure))) {
            const auto failed_initialization = endpoint_->initialization();
            CleanupEndpointOnAudioThread();
            SignalInitializationFailure(failure, failed_initialization);
            SetEvent(audio_exited_event_);
            return;
        }
        clock_mapper_.Reset(
            initial_clock.position,
            initialization_.clock_frequency,
            0);
        last_qpc_100ns_ = initial_clock.qpc_100ns;
        actual_period_100ns_ = FramesToReferenceTime(
            frames, kOutputSampleRate);
        initialization_succeeded_.store(true, std::memory_order_release);
        SetEvent(initialization_event_);

        RenderLoop();
        CleanupEndpointOnAudioThread();
    } catch (const std::bad_alloc&) {
        failure = {AudioFailureStage::InitializeMixer, E_OUTOFMEMORY};
        const auto failed_initialization = endpoint_ != nullptr
            ? endpoint_->initialization()
            : attempted;
        CleanupEndpointOnAudioThread();
        SignalInitializationFailure(failure, failed_initialization);
    } catch (...) {
        failure = {AudioFailureStage::InitializeMixer, E_UNEXPECTED};
        const auto failed_initialization = endpoint_ != nullptr
            ? endpoint_->initialization()
            : attempted;
        CleanupEndpointOnAudioThread();
        SignalInitializationFailure(failure, failed_initialization);
    }
    SetEvent(audio_exited_event_);
}

void ExclusiveAudioEngine::RenderLoop() noexcept {
    const auto frames = endpoint_buffer_frames_.load(std::memory_order_acquire);
    while (!ShutdownRequested() &&
           failure_stage_.load(std::memory_order_acquire) ==
               static_cast<std::uint32_t>(AudioFailureStage::None)) {
        AudioFailure failure{};
        if (FAILED(endpoint_->WaitForRender(kRenderWaitMs, &failure))) {
            if (!ShutdownRequested()) {
                RecordRuntimeFailure(failure);
            }
            break;
        }
        if (ShutdownRequested() ||
            failure_stage_.load(std::memory_order_acquire) !=
                static_cast<std::uint32_t>(AudioFailureStage::None)) {
            break;
        }

        EndpointClockPosition clock{};
        if (FAILED(endpoint_->ReadClock(&clock, &failure))) {
            static_cast<void>(endpoint_->TrySubmitSilence());
            RecordRuntimeFailure(failure);
            break;
        }
        CountLateWake(clock.qpc_100ns);

        const auto begin = submitted_frames_.load(std::memory_order_relaxed);
        auto rendered = mixer_->Render(float_mix_, begin);
#if defined(GC_EXCLUSIVE_AUDIO_ENGINE_TESTING)
        // The real mixer always requests a complete fixed period. The fake
        // endpoint uses this impossible clock position to exercise the
        // defensive successful-short-read branch deterministically.
        if (clock.position == std::numeric_limits<std::uint64_t>::max() &&
            rendered.result == MA_SUCCESS) {
            rendered.frames_read = frames / 2;
        }
#endif
        if (rendered.result != MA_SUCCESS) {
            std::fill(float_mix_.begin(), float_mix_.end(), 0.0F);
            silence_fallbacks_.fetch_add(1, std::memory_order_relaxed);
        } else if (rendered.frames_read != frames) {
            const auto bounded_frames = std::min<std::uint64_t>(
                rendered.frames_read, frames);
            const auto first_missing =
                static_cast<std::size_t>(bounded_frames) * kOutputChannels;
            std::fill(
                float_mix_.begin() +
                    static_cast<std::ptrdiff_t>(first_missing),
                float_mix_.end(),
                0.0F);
            silence_fallbacks_.fetch_add(1, std::memory_order_relaxed);
        }
        ConvertFloatToPcm16(float_mix_, pcm16_mix_);
        if (FAILED(endpoint_->SubmitPcm16(pcm16_mix_, &failure))) {
            RecordRuntimeFailure(failure);
            break;
        }
        submitted_frames_.fetch_add(frames, std::memory_order_release);
        render_callbacks_.fetch_add(1, std::memory_order_relaxed);
    }
}

void ExclusiveAudioEngine::MonitorThreadMain() noexcept {
    if (WaitForSingleObject(initialization_event_, INFINITE) != WAIT_OBJECT_0 ||
        !initialization_succeeded_.load(std::memory_order_acquire)) {
        return;
    }

    const HANDLE controls[]{fatal_event_, shutdown_event_};
    for (;;) {
        const auto wait = WaitForMultipleObjects(
            static_cast<DWORD>(std::size(controls)),
            controls,
            FALSE,
            kSummaryIntervalMs);
        if (wait == WAIT_OBJECT_0) {
            const auto stage = static_cast<AudioFailureStage>(
                failure_stage_.load(std::memory_order_acquire));
            const auto result = static_cast<HRESULT>(
                failure_result_.load(std::memory_order_relaxed));
            observer_->RuntimeFailed({stage, result}, SnapshotCounters());
            return;
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            return;
        }
        if (wait == WAIT_TIMEOUT) {
            observer_->RuntimeSummary(SnapshotCounters());
            continue;
        }
        return;
    }
}

void ExclusiveAudioEngine::CleanupEndpointOnAudioThread() noexcept {
    if (endpoint_ == nullptr) {
        return;
    }
    static_cast<void>(endpoint_->ShutdownOnInitializingThread());
    endpoint_.reset();
}

void ExclusiveAudioEngine::SignalInitializationFailure(
    const AudioFailure& failure,
    const EndpointInitialization& attempted) noexcept {
    startup_failure_ = {failure, attempted};
    initialization_succeeded_.store(false, std::memory_order_release);
    SetEvent(initialization_event_);
}

void ExclusiveAudioEngine::RecordRuntimeFailure(
    const AudioFailure& failure) noexcept {
    endpoint_hresult_failures_.fetch_add(1, std::memory_order_relaxed);
    bool expected = false;
    if (!failure_claimed_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
        return;
    }
    failure_result_.store(failure.result, std::memory_order_relaxed);
    failure_stage_.store(
        static_cast<std::uint32_t>(failure.stage),
        std::memory_order_release);
    SetEvent(fatal_event_);
}

void ExclusiveAudioEngine::CountLateWake(std::uint64_t qpc_100ns) noexcept {
    if (qpc_100ns > last_qpc_100ns_) {
        const auto delta = qpc_100ns - last_qpc_100ns_;
        const auto period = static_cast<std::uint64_t>(
            std::max<REFERENCE_TIME>(actual_period_100ns_, 0));
        if (delta > period + period / 2) {
            late_event_wakes_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    last_qpc_100ns_ = qpc_100ns;
}

AudioRuntimeCountersSnapshot
ExclusiveAudioEngine::SnapshotCounters() const noexcept {
    return {
        render_callbacks_.load(std::memory_order_relaxed),
        late_event_wakes_.load(std::memory_order_relaxed),
        silence_fallbacks_.load(std::memory_order_relaxed),
        cursor_timeline_failures_.load(std::memory_order_relaxed),
        endpoint_hresult_failures_.load(std::memory_order_relaxed),
        mixer_ != nullptr ? mixer_->diagnostics() : MixerDiagnosticsSnapshot{},
    };
}

bool ExclusiveAudioEngine::ShutdownRequested() const noexcept {
    return shutdown_event_ != nullptr &&
        WaitForSingleObject(shutdown_event_, 0) == WAIT_OBJECT_0;
}

void ExclusiveAudioEngine::CloseControlEvents() noexcept {
    const HANDLE handles[]{
        initialization_event_, fatal_event_, shutdown_event_,
        audio_exited_event_};
    for (const auto handle : handles) {
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }
    initialization_event_ = nullptr;
    fatal_event_ = nullptr;
    shutdown_event_ = nullptr;
    audio_exited_event_ = nullptr;
}

std::unique_ptr<MixerVoice> ExclusiveAudioEngine::CreateVoice(
    const NormalizedSourceFormat& format,
    std::shared_ptr<AudioSnapshot> snapshot,
    std::shared_ptr<AudioCursorTimeline> timeline,
    VoiceUsage usage,
    ma_result* result) noexcept {
    if (mixer_ == nullptr) {
        if (result != nullptr) {
            *result = MA_INVALID_OPERATION;
        }
        return nullptr;
    }
    return mixer_->CreateVoice(
        format,
        std::move(snapshot),
        std::move(timeline),
        usage,
        result);
}

std::optional<std::uint64_t>
ExclusiveAudioEngine::CurrentOutputFrame() noexcept {
    if (endpoint_ == nullptr) {
        return std::nullopt;
    }
    EndpointClockPosition position{};
    AudioFailure failure{};
    if (FAILED(endpoint_->ReadClock(&position, &failure))) {
        RecordRuntimeFailure(failure);
        return std::nullopt;
    }
    return clock_mapper_.ToOutputFrame(position.position);
}

std::uint32_t ExclusiveAudioEngine::endpoint_buffer_frames() const noexcept {
    return endpoint_buffer_frames_.load(std::memory_order_acquire);
}

void ExclusiveAudioEngine::CountCursorTimelineFailure() noexcept {
    cursor_timeline_failures_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace gc::audio
