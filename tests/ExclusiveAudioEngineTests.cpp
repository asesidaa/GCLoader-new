#include "ExclusiveAudioEngine.h"

#include <audioclient.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;
using gc::audio::AudioCursorTimeline;
using gc::audio::AudioFailure;
using gc::audio::AudioFailureStage;
using gc::audio::AudioLockRegions;
using gc::audio::AudioRuntimeCountersSnapshot;
using gc::audio::AudioSnapshot;
using gc::audio::AudioStartupFailure;
using gc::audio::EndpointInitialization;
using gc::audio::ExclusiveAudioEngine;
using gc::audio::IAudioEngineObserver;
using gc::audio::IWasapiApi;
using gc::audio::NormalizedSourceFormat;
using gc::audio::VoiceUsage;

constexpr std::uint32_t kFrames = 8;
constexpr std::size_t kSamples = kFrames * gc::audio::kOutputChannels;
constexpr std::uint64_t kClockFrequency = 10'000'000;
constexpr std::uint64_t kInitialClock = 1'000;
constexpr auto kPeriod = static_cast<REFERENCE_TIME>(1'814);
constexpr HRESULT kWaitFailure = HRESULT_FROM_WIN32(ERROR_TIMEOUT);

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

struct AllocationProbe {
    std::atomic_bool enabled{};
    std::atomic_uint64_t active_callbacks{};
    std::atomic_uint64_t lifetime_callbacks{};

    static void* Allocate(std::size_t size, void* user_data) {
        auto& probe = *static_cast<AllocationProbe*>(user_data);
        probe.lifetime_callbacks.fetch_add(1, std::memory_order_relaxed);
        if (probe.enabled.load(std::memory_order_relaxed)) {
            probe.active_callbacks.fetch_add(1, std::memory_order_relaxed);
        }
        return std::malloc(size == 0 ? 1 : size);
    }

    static void* Reallocate(
        void* pointer, std::size_t size, void* user_data) {
        auto& probe = *static_cast<AllocationProbe*>(user_data);
        probe.lifetime_callbacks.fetch_add(1, std::memory_order_relaxed);
        if (probe.enabled.load(std::memory_order_relaxed)) {
            probe.active_callbacks.fetch_add(1, std::memory_order_relaxed);
        }
        return std::realloc(pointer, size == 0 ? 1 : size);
    }

    static void Free(void* pointer, void* user_data) {
        auto& probe = *static_cast<AllocationProbe*>(user_data);
        probe.lifetime_callbacks.fetch_add(1, std::memory_order_relaxed);
        if (probe.enabled.load(std::memory_order_relaxed)) {
            probe.active_callbacks.fetch_add(1, std::memory_order_relaxed);
        }
        std::free(pointer);
    }

    ma_allocation_callbacks Callbacks() noexcept {
        return {this, Allocate, Reallocate, Free};
    }

    void Begin() noexcept {
        active_callbacks.store(0, std::memory_order_relaxed);
        enabled.store(true, std::memory_order_release);
    }

    std::uint64_t End() noexcept {
        enabled.store(false, std::memory_order_release);
        return active_callbacks.load(std::memory_order_relaxed);
    }
};

enum class ApiCall : std::uint8_t {
    InitializeComMta,
    OpenDefaultConsoleEndpoint,
    ActivateAudioClient,
    IsExactFormatSupported,
    GetDevicePeriod,
    InitializeExclusiveEvent,
    GetBufferSize,
    ReleaseAudioClient,
    CreateRenderEvent,
    SetEventHandle,
    GetRenderService,
    GetClockService,
    GetClockFrequency,
    GetRenderBuffer,
    ReleaseRenderBuffer,
    RegisterMmcssProAudio,
    SetMmcssCriticalPriority,
    Start,
    WaitForRender,
    GetClockPosition,
    ShutdownOnInitializingThread,
    Destroy,
};

struct CallRecord {
    ApiCall call{};
    DWORD thread_id{};
};

struct WaitAction {
    HRESULT result{S_OK};
};

struct ClockAction {
    HRESULT result{S_OK};
    std::uint64_t position{kInitialClock};
    std::uint64_t qpc_100ns{kInitialClock};
};

struct FakeWasapiState {
    std::array<CallRecord, 256> calls{};
    std::atomic_size_t call_count{};
    std::array<WaitAction, 64> waits{};
    std::size_t wait_read{};
    std::size_t wait_write{};
    std::array<ClockAction, 64> clocks{};
    std::size_t clock_read{};
    std::size_t clock_write{};
    std::array<std::array<std::int16_t, kSamples>, 32> submissions{};
    std::atomic_size_t submission_count{};
    std::array<std::byte, kSamples * sizeof(std::int16_t)> render_bytes{};
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic_bool block_initialize{};
    std::atomic_bool release_initialize{};
    std::atomic_bool started{};
    std::atomic_bool render_probe{};
    std::atomic_bool mixer_observed_before_start{};
    std::atomic_uint32_t shutdown_calls{};
    std::atomic_uint32_t destroy_calls{};
    std::atomic<DWORD> audio_thread_id{};
    std::atomic<DWORD> shutdown_thread_id{};
    std::atomic<DWORD> destroy_thread_id{};
    ApiCall failure_call{ApiCall::Destroy};
    HRESULT failure_result{S_OK};
    std::uint32_t failure_occurrence{1};
    std::uint32_t matching_calls{};
    AllocationProbe* allocations{};

    void Record(ApiCall call) noexcept {
        const auto index = call_count.fetch_add(1, std::memory_order_relaxed);
        if (index < calls.size()) {
            calls[index] = {call, GetCurrentThreadId()};
        }
    }

    HRESULT MaybeFail(ApiCall call) noexcept {
        if (call != failure_call) {
            return S_OK;
        }
        ++matching_calls;
        return matching_calls == failure_occurrence ? failure_result : S_OK;
    }

    void PushWait(HRESULT result = S_OK) {
        {
            std::lock_guard lock(mutex);
            waits[wait_write++ % waits.size()] = {result};
        }
        condition.notify_all();
    }

    void PushClock(
        HRESULT result,
        std::uint64_t position = kInitialClock,
        std::uint64_t qpc_100ns = kInitialClock) {
        std::lock_guard lock(mutex);
        clocks[clock_write++ % clocks.size()] = {
            result, position, qpc_100ns};
    }

    void PushClock(
        std::uint64_t position,
        std::uint64_t qpc_100ns) {
        PushClock(S_OK, position, qpc_100ns);
    }

    std::size_t Count(ApiCall wanted) const noexcept {
        const auto count = std::min(call_count.load(), calls.size());
        std::size_t found{};
        for (std::size_t index = 0; index < count; ++index) {
            found += calls[index].call == wanted ? 1 : 0;
        }
        return found;
    }
};

class FakeWasapiApi final : public IWasapiApi {
public:
    explicit FakeWasapiApi(std::shared_ptr<FakeWasapiState> state) noexcept
        : state_(std::move(state)) {}

    ~FakeWasapiApi() override {
        state_->Record(ApiCall::Destroy);
        state_->destroy_calls.fetch_add(1);
        state_->destroy_thread_id.store(GetCurrentThreadId());
    }

    HRESULT InitializeComMta() noexcept override {
        state_->Record(ApiCall::InitializeComMta);
        state_->audio_thread_id.store(GetCurrentThreadId());
        if (state_->block_initialize.load()) {
            std::unique_lock lock(state_->mutex);
            state_->condition.wait(lock, [&] {
                return state_->release_initialize.load();
            });
        }
        return state_->MaybeFail(ApiCall::InitializeComMta);
    }

    HRESULT OpenDefaultConsoleEndpoint(
        std::wstring* name, std::wstring* id) noexcept override {
        state_->Record(ApiCall::OpenDefaultConsoleEndpoint);
        *name = L"Fake Speakers";
        *id = L"fake-endpoint-id";
        return state_->MaybeFail(ApiCall::OpenDefaultConsoleEndpoint);
    }

    HRESULT ActivateAudioClient() noexcept override {
        state_->Record(ApiCall::ActivateAudioClient);
        return state_->MaybeFail(ApiCall::ActivateAudioClient);
    }

    HRESULT IsExactFormatSupported(const WAVEFORMATEX&) noexcept override {
        state_->Record(ApiCall::IsExactFormatSupported);
        return state_->MaybeFail(ApiCall::IsExactFormatSupported);
    }

    HRESULT GetDevicePeriod(
        REFERENCE_TIME* default_period,
        REFERENCE_TIME* minimum_period) noexcept override {
        state_->Record(ApiCall::GetDevicePeriod);
        *default_period = kPeriod * 2;
        *minimum_period = kPeriod;
        return state_->MaybeFail(ApiCall::GetDevicePeriod);
    }

    HRESULT InitializeExclusiveEvent(
        REFERENCE_TIME,
        REFERENCE_TIME,
        const WAVEFORMATEX&) noexcept override {
        state_->Record(ApiCall::InitializeExclusiveEvent);
        return state_->MaybeFail(ApiCall::InitializeExclusiveEvent);
    }

    HRESULT GetBufferSize(std::uint32_t* frames) noexcept override {
        state_->Record(ApiCall::GetBufferSize);
        *frames = static_cast<std::uint32_t>(
            gc::audio::ReferenceTimeToFramesCeil(
                kPeriod, gc::audio::kOutputSampleRate));
        return state_->MaybeFail(ApiCall::GetBufferSize);
    }

    void ReleaseAudioClient() noexcept override {
        state_->Record(ApiCall::ReleaseAudioClient);
    }

    HRESULT CreateRenderEvent() noexcept override {
        state_->Record(ApiCall::CreateRenderEvent);
        return state_->MaybeFail(ApiCall::CreateRenderEvent);
    }

    HRESULT SetEventHandle() noexcept override {
        state_->Record(ApiCall::SetEventHandle);
        return state_->MaybeFail(ApiCall::SetEventHandle);
    }

    HRESULT GetRenderService() noexcept override {
        state_->Record(ApiCall::GetRenderService);
        return state_->MaybeFail(ApiCall::GetRenderService);
    }

    HRESULT GetClockService() noexcept override {
        state_->Record(ApiCall::GetClockService);
        return state_->MaybeFail(ApiCall::GetClockService);
    }

    HRESULT GetClockFrequency(std::uint64_t* frequency) noexcept override {
        state_->Record(ApiCall::GetClockFrequency);
        *frequency = kClockFrequency;
        return state_->MaybeFail(ApiCall::GetClockFrequency);
    }

    HRESULT GetRenderBuffer(
        std::uint32_t frames, BYTE** buffer) noexcept override {
        state_->Record(ApiCall::GetRenderBuffer);
        const auto failure = state_->MaybeFail(ApiCall::GetRenderBuffer);
        if (FAILED(failure)) {
            return failure;
        }
        if (frames != kFrames) {
            return E_INVALIDARG;
        }
        *buffer = reinterpret_cast<BYTE*>(state_->render_bytes.data());
        return S_OK;
    }

    HRESULT ReleaseRenderBuffer(
        std::uint32_t frames, DWORD flags) noexcept override {
        state_->Record(ApiCall::ReleaseRenderBuffer);
        const auto failure = state_->MaybeFail(ApiCall::ReleaseRenderBuffer);
        if (FAILED(failure)) {
            return failure;
        }
        if (frames != kFrames) {
            return E_INVALIDARG;
        }
        if (state_->started.load() && flags == 0) {
            const auto index = state_->submission_count.fetch_add(1);
            if (index < state_->submissions.size()) {
                std::memcpy(
                    state_->submissions[index].data(),
                    state_->render_bytes.data(),
                    state_->render_bytes.size());
            }
        }
        return S_OK;
    }

    HRESULT RegisterMmcssProAudio() noexcept override {
        state_->Record(ApiCall::RegisterMmcssProAudio);
        return state_->MaybeFail(ApiCall::RegisterMmcssProAudio);
    }

    HRESULT SetMmcssCriticalPriority() noexcept override {
        state_->Record(ApiCall::SetMmcssCriticalPriority);
        return state_->MaybeFail(ApiCall::SetMmcssCriticalPriority);
    }

    HRESULT Start() noexcept override {
        state_->Record(ApiCall::Start);
        state_->mixer_observed_before_start.store(
            state_->allocations != nullptr &&
            state_->allocations->lifetime_callbacks.load() != 0);
        const auto failure = state_->MaybeFail(ApiCall::Start);
        if (SUCCEEDED(failure)) {
            state_->started.store(true);
        }
        return failure;
    }

    HRESULT WaitForRender(DWORD) noexcept override {
        state_->Record(ApiCall::WaitForRender);
        std::unique_lock lock(state_->mutex);
        state_->condition.wait_for(lock, 500ms, [&] {
            return state_->wait_read != state_->wait_write;
        });
        if (state_->wait_read == state_->wait_write) {
            return kWaitFailure;
        }
        return state_->waits[state_->wait_read++ % state_->waits.size()].result;
    }

    HRESULT GetClockPosition(
        std::uint64_t* position,
        std::uint64_t* qpc_100ns) noexcept override {
        state_->Record(ApiCall::GetClockPosition);
        std::lock_guard lock(state_->mutex);
        ClockAction action{};
        if (state_->clock_read != state_->clock_write) {
            action = state_->clocks[
                state_->clock_read++ % state_->clocks.size()];
        }
        *position = action.position;
        *qpc_100ns = action.qpc_100ns;
        return action.result;
    }

    HRESULT ShutdownOnInitializingThread() noexcept override {
        state_->Record(ApiCall::ShutdownOnInitializingThread);
        state_->shutdown_calls.fetch_add(1);
        state_->shutdown_thread_id.store(GetCurrentThreadId());
        return GetCurrentThreadId() == state_->audio_thread_id.load()
            ? S_OK
            : RPC_E_WRONG_THREAD;
    }

private:
    std::shared_ptr<FakeWasapiState> state_;
};

struct ObserverState {
    std::mutex mutex;
    std::condition_variable condition;
    EndpointInitialization startup{};
    AudioRuntimeCountersSnapshot summary{};
    AudioRuntimeCountersSnapshot fatal_counters{};
    AudioFailure fatal_failure{};
    std::atomic_uint32_t startup_count{};
    std::atomic_uint32_t summary_count{};
    std::atomic_uint32_t fatal_count{};
    std::atomic_uint32_t callback_count{};
    std::atomic_uint32_t render_callback_violations{};
    std::atomic<DWORD> startup_thread_id{};
    std::atomic<DWORD> summary_thread_id{};
    std::atomic<DWORD> fatal_thread_id{};
    std::atomic_bool* render_probe{};
};

class FakeObserver final : public IAudioEngineObserver {
public:
    explicit FakeObserver(std::shared_ptr<ObserverState> state) noexcept
        : state_(std::move(state)) {}

    void StartupSucceeded(
        const EndpointInitialization& initialization) noexcept override {
        CallbackProbe();
        {
            std::lock_guard lock(state_->mutex);
            state_->startup = initialization;
            state_->startup_thread_id.store(GetCurrentThreadId());
            state_->startup_count.fetch_add(1);
        }
        state_->condition.notify_all();
    }

    void RuntimeSummary(
        const AudioRuntimeCountersSnapshot& counters) noexcept override {
        CallbackProbe();
        {
            std::lock_guard lock(state_->mutex);
            state_->summary = counters;
            state_->summary_thread_id.store(GetCurrentThreadId());
            state_->summary_count.fetch_add(1);
        }
        state_->condition.notify_all();
    }

    void RuntimeFailed(
        const AudioFailure& failure,
        const AudioRuntimeCountersSnapshot& counters) noexcept override {
        CallbackProbe();
        {
            std::lock_guard lock(state_->mutex);
            state_->fatal_failure = failure;
            state_->fatal_counters = counters;
            state_->fatal_thread_id.store(GetCurrentThreadId());
            state_->fatal_count.fetch_add(1);
        }
        state_->condition.notify_all();
    }

private:
    void CallbackProbe() noexcept {
        state_->callback_count.fetch_add(1);
        if (state_->render_probe != nullptr &&
            state_->render_probe->load(std::memory_order_acquire)) {
            state_->render_callback_violations.fetch_add(1);
        }
    }

    std::shared_ptr<ObserverState> state_;
};

struct EngineFixture {
    std::shared_ptr<FakeWasapiState> api =
        std::make_shared<FakeWasapiState>();
    std::shared_ptr<ObserverState> observer_state =
        std::make_shared<ObserverState>();
    std::shared_ptr<FakeObserver> observer =
        std::make_shared<FakeObserver>(observer_state);
    AllocationProbe allocations;
    ma_allocation_callbacks callbacks = allocations.Callbacks();

    EngineFixture() {
        api->allocations = &allocations;
        observer_state->render_probe = &api->render_probe;
        api->PushClock(S_OK, kInitialClock, kInitialClock);
    }

    std::unique_ptr<ExclusiveAudioEngine> Start(
        AudioStartupFailure* failure = nullptr,
        DWORD timeout_ms = 2'000) {
        return ExclusiveAudioEngine::StartAndWait(
            std::make_unique<FakeWasapiApi>(api),
            observer,
            timeout_ms,
            &callbacks,
            failure);
    }
};

WAVEFORMATEX Pcm16Mono() noexcept {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = gc::audio::kOutputSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec =
        format.nSamplesPerSec * format.nBlockAlign;
    return format;
}

struct TestSource {
    NormalizedSourceFormat format{};
    std::shared_ptr<AudioSnapshot> snapshot;
    std::shared_ptr<AudioCursorTimeline> timeline;
};

TestSource MakeConstantSource(int& failures) {
    TestSource source{};
    const auto wave = Pcm16Mono();
    failures += Expect(
        gc::audio::NormalizeSourceFormat(&wave, &source.format) == DS_OK,
        "native PCM16 source normalization");
    source.snapshot = std::make_shared<AudioSnapshot>(
        kFrames * source.format.block_align,
        source.format.block_align);
    source.timeline = std::make_shared<AudioCursorTimeline>();
    AudioLockRegions regions{};
    failures += Expect(
        source.snapshot->Lock(
            0,
            source.snapshot->byte_length(),
            DSBLOCK_ENTIREBUFFER,
            &regions) == DS_OK,
        "source snapshot lock");
    std::array<std::int16_t, kFrames> samples{};
    samples.fill(16'384);
    std::memcpy(regions.first, samples.data(), regions.first_bytes);
    if (regions.second_bytes != 0) {
        std::memcpy(
            regions.second,
            reinterpret_cast<const std::byte*>(samples.data()) +
                regions.first_bytes,
            regions.second_bytes);
    }
    failures += Expect(
        source.snapshot->Unlock(
            regions.first,
            regions.first_bytes,
            regions.second,
            regions.second_bytes) == DS_OK,
        "source snapshot publication");
    return source;
}

bool WaitForFatal(const EngineFixture& fixture) {
    return WaitUntil([&] {
        return fixture.observer_state->fatal_count.load() != 0;
    });
}

int ExpectOwnerThreadCleanup(
    const EngineFixture& fixture,
    std::string_view name) {
    int failures = 0;
    const auto audio = fixture.api->audio_thread_id.load();
    failures += Expect(audio != 0, name);
    failures += Expect(
        fixture.api->shutdown_calls.load() == 1,
        "exactly one endpoint shutdown");
    failures += Expect(
        fixture.api->destroy_calls.load() == 1,
        "endpoint reset destroys API before join completes");
    failures += Expect(
        fixture.api->shutdown_thread_id.load() == audio,
        "shutdown runs on initializing audio thread");
    failures += Expect(
        fixture.api->destroy_thread_id.load() == audio,
        "endpoint reset runs on initializing audio thread");
    failures += Expect(
        audio != GetCurrentThreadId(),
        "caller never owns endpoint shutdown");
    return failures;
}

int TestBoundedInitializationTimeout() {
    int failures = 0;
    EngineFixture fixture;
    fixture.api->block_initialize.store(true);
    AudioStartupFailure failure{};
    const auto before = std::chrono::steady_clock::now();
    auto engine = fixture.Start(&failure, 5);
    const auto elapsed = std::chrono::steady_clock::now() - before;
    failures += Expect(engine == nullptr, "timeout returns no engine");
    failures += Expect(
        failure.failure.stage == AudioFailureStage::InitializationTimeout &&
            failure.failure.result == HRESULT_FROM_WIN32(ERROR_TIMEOUT),
        "exact initialization-timeout failure");
    failures += Expect(elapsed < 100ms, "bounded startup wait");
    failures += Expect(
        fixture.api->shutdown_calls.load() == 0,
        "timeout caller deliberately does not destroy stuck endpoint");
    fixture.api->release_initialize.store(true);
    fixture.api->condition.notify_all();
    failures += Expect(
        WaitUntil([&] { return fixture.api->destroy_calls.load() == 1; }),
        "abandoned timeout object eventually performs owner-thread cleanup");
    return failures;
}

int TestStartupFailuresAndStartOrdering() {
    int failures = 0;
    {
        EngineFixture fixture;
        fixture.api->failure_call = ApiCall::CreateRenderEvent;
        fixture.api->failure_result = E_ACCESSDENIED;
        AudioStartupFailure failure{};
        auto engine = fixture.Start(&failure);
        failures += Expect(engine == nullptr, "endpoint initialization failure");
        failures += Expect(
            failure.failure.stage == AudioFailureStage::CreateRenderEvent &&
                failure.failure.result == E_ACCESSDENIED,
            "exact endpoint startup stage and HRESULT");
        failures += Expect(
            failure.attempted.endpoint_name == L"Fake Speakers" &&
                failure.attempted.endpoint_id == L"fake-endpoint-id" &&
                failure.attempted.actual_buffer_frames == kFrames,
            "attempted endpoint metadata survives failure");
        failures += ExpectOwnerThreadCleanup(fixture, "failure audio thread");
    }
    {
        EngineFixture fixture;
        fixture.api->failure_call = ApiCall::Start;
        fixture.api->failure_result = AUDCLNT_E_DEVICE_INVALIDATED;
        AudioStartupFailure failure{};
        auto engine = fixture.Start(&failure);
        failures += Expect(engine == nullptr, "post-create Start failure");
        failures += Expect(
            failure.failure.stage == AudioFailureStage::StartEndpoint &&
                failure.failure.result == AUDCLNT_E_DEVICE_INVALIDATED,
            "exact Start failure handoff");
        failures += Expect(
            fixture.api->mixer_observed_before_start.load(),
            "mixer allocation exists before endpoint Start");
        failures += ExpectOwnerThreadCleanup(fixture, "Start failure audio thread");
    }
    {
        EngineFixture fixture;
        fixture.api->clock_read = 0;
        fixture.api->clock_write = 0;
        fixture.api->PushClock(E_FAIL);
        AudioStartupFailure failure{};
        auto engine = fixture.Start(&failure);
        failures += Expect(engine == nullptr, "initial clock setup failure");
        failures += Expect(
            failure.failure.stage == AudioFailureStage::GetClockPosition &&
                failure.failure.result == E_FAIL,
            "exact initial-clock startup failure handoff");
        failures += ExpectOwnerThreadCleanup(
            fixture, "initial clock failure audio thread");
    }
    return failures;
}

int TestSilenceRuntimeFailureAndCleanup() {
    int failures = 0;
    EngineFixture fixture;
    auto engine = fixture.Start();
    failures += Expect(engine != nullptr, "successful engine startup");
    failures += Expect(
        fixture.observer_state->startup_count.load() == 1 &&
            fixture.observer_state->startup_thread_id.load() ==
                GetCurrentThreadId(),
        "startup observer runs on waiting caller");
    failures += Expect(
        engine != nullptr && engine->endpoint_buffer_frames() == kFrames,
        "authoritative endpoint frame count");

    for (std::uint64_t index = 0; index < 3; ++index) {
        fixture.api->PushClock(
            kInitialClock + (index + 1) * kPeriod,
            kInitialClock + (index + 1) * kPeriod);
        fixture.api->PushWait();
    }
    failures += Expect(
        WaitUntil([&] { return fixture.api->submission_count.load() == 3; }),
        "three silence submissions complete");
    for (std::size_t block = 0; block < 3; ++block) {
        failures += Expect(
            std::all_of(
                fixture.api->submissions[block].begin(),
                fixture.api->submissions[block].end(),
                [](std::int16_t sample) { return sample == 0; }),
            "zero-voice block is all-zero PCM16");
    }

    fixture.api->PushWait(E_ABORT);
    failures += Expect(WaitForFatal(fixture), "runtime wait failure reported");
    failures += Expect(
        fixture.observer_state->fatal_failure.stage ==
                AudioFailureStage::WaitRenderEvent &&
            fixture.observer_state->fatal_failure.result == E_ABORT,
        "exact runtime wait failure handoff");
    failures += Expect(
        fixture.observer_state->fatal_counters.render_callbacks == 3 &&
            fixture.observer_state->fatal_counters.endpoint_hresult_failures ==
                1,
        "runtime render and endpoint counters");
    failures += Expect(
        fixture.observer_state->fatal_thread_id.load() !=
                fixture.api->audio_thread_id.load() &&
            fixture.observer_state->fatal_thread_id.load() !=
                GetCurrentThreadId(),
        "fatal observer runs only on monitor thread");
    engine.reset();
    failures += ExpectOwnerThreadCleanup(fixture, "runtime failure audio thread");
    return failures;
}

int TestVoiceClockSummaryAndRenderSafety() {
    int failures = 0;
    EngineFixture fixture;
    auto engine = fixture.Start();
    failures += Expect(engine != nullptr, "voice engine startup");
    auto source = MakeConstantSource(failures);
    ma_result voice_result = MA_ERROR;
    auto voice = engine->CreateVoice(
        source.format,
        source.snapshot,
        source.timeline,
        VoiceUsage::GameplayNativeCandidate,
        &voice_result);
    failures += Expect(
        voice != nullptr && voice_result == MA_SUCCESS,
        "snapshot-backed engine voice");
    failures += Expect(
        voice != nullptr && voice->Play(true, 1) == DS_OK,
        "looping voice starts");

    const auto late_qpc = kInitialClock + kPeriod + kPeriod / 2 + 1;
    fixture.api->PushClock(kInitialClock + kPeriod, late_qpc);
    fixture.api->PushWait();
    failures += Expect(
        WaitUntil([&] { return fixture.api->submission_count.load() >= 1; }),
        "nonzero voice render");
    failures += Expect(
        std::any_of(
            fixture.api->submissions[0].begin(),
            fixture.api->submissions[0].end(),
            [](std::int16_t sample) { return sample != 0; }),
        "voice produces nonzero PCM16");

    fixture.api->PushClock(
        std::numeric_limits<std::uint64_t>::max(),
        late_qpc + kPeriod);
    fixture.api->PushWait();
    failures += Expect(
        WaitUntil([&] { return fixture.api->submission_count.load() >= 2; }),
        "forced short-read render");
    failures += Expect(
        std::all_of(
            fixture.api->submissions[1].begin() + kSamples / 2,
            fixture.api->submissions[1].end(),
            [](std::int16_t sample) { return sample == 0; }),
        "short-read remainder is zero-filled");

    const auto callbacks_before =
        fixture.observer_state->callback_count.load();
    fixture.allocations.Begin();
    fixture.api->render_probe.store(true, std::memory_order_release);
    for (std::uint64_t index = 0; index < 2; ++index) {
        fixture.api->PushClock(
            kInitialClock + (index + 3) * kPeriod,
            late_qpc + (index + 2) * kPeriod);
        fixture.api->PushWait();
    }
    failures += Expect(
        WaitUntil([&] { return fixture.api->submission_count.load() >= 4; }),
        "two warmed render events");
    fixture.api->render_probe.store(false, std::memory_order_release);
    failures += Expect(
        fixture.allocations.End() == 0,
        "zero mixer allocation/free callbacks in warmed render events");
    failures += Expect(
        fixture.observer_state->callback_count.load() == callbacks_before &&
            fixture.observer_state->render_callback_violations.load() == 0,
        "no observer callback in warmed render events");

    failures += Expect(
        source.timeline->ResolveSourceFrame(0, 1, kFrames).has_value() &&
            source.timeline->ResolveSourceFrame(kFrames, 1, kFrames)
                .has_value(),
        "voice timeline advances by actual submitted endpoint frames");

    fixture.api->PushClock(
        kInitialClock + kClockFrequency,
        late_qpc + 4 * kPeriod);
    const auto mapped = engine->CurrentOutputFrame();
    failures += Expect(
        mapped.has_value() && *mapped == gc::audio::kOutputSampleRate,
        "CurrentOutputFrame maps IAudioClock through endpoint mapper");

    fixture.api->PushClock(S_OK, kInitialClock - 1, late_qpc + 5 * kPeriod);
    failures += Expect(
        !engine->CurrentOutputFrame().has_value(),
        "unmappable successful clock sample returns no frame");
    engine->CountCursorTimelineFailure();

    failures += Expect(
        WaitUntil([&] {
            std::lock_guard lock(fixture.observer_state->mutex);
            const auto& counters = fixture.observer_state->summary;
            return fixture.observer_state->summary_count.load() != 0 &&
                counters.render_callbacks >= 4 &&
                counters.late_event_wakes == 1 &&
                counters.silence_fallbacks == 1 &&
                counters.cursor_timeline_failures == 1;
        }),
        "periodic summary includes final render counters");
    {
        std::lock_guard lock(fixture.observer_state->mutex);
        const auto& summary = fixture.observer_state->summary;
        failures += Expect(
            summary.endpoint_hresult_failures == 0,
            "unmappable clock increments only cursor timeline failures");
        failures += Expect(
            summary.mixer.native_rate_buffers == 1 &&
                summary.mixer.sample_format_converted_buffers == 1 &&
                summary.mixer.sample_rate_converted_buffers == 0 &&
                summary.mixer.native_gameplay_buffers == 1 &&
                summary.mixer.active_voices == 1 &&
                summary.mixer.maximum_simultaneous_voices == 1,
            "periodic summary includes mixer classification and maximum");
    }
    failures += Expect(
        fixture.observer_state->summary_thread_id.load() !=
            fixture.api->audio_thread_id.load(),
        "summary callback runs on monitor thread");

    voice->Stop();
    fixture.api->PushWait(E_ABORT);
    failures += Expect(WaitForFatal(fixture), "voice engine fatal exit");
    engine.reset();
    voice.reset();
    failures += ExpectOwnerThreadCleanup(fixture, "voice runtime audio thread");
    return failures;
}

struct RuntimeFailureCase {
    ApiCall call;
    std::uint32_t occurrence;
    AudioFailureStage stage;
    HRESULT result;
    bool current_output_frame{};
};

int TestExactRuntimeFailureStages() {
    constexpr std::array cases{
        RuntimeFailureCase{
            ApiCall::GetRenderBuffer,
            2,
            AudioFailureStage::GetRenderBuffer,
            E_ACCESSDENIED},
        RuntimeFailureCase{
            ApiCall::ReleaseRenderBuffer,
            2,
            AudioFailureStage::ReleaseRenderBuffer,
            E_HANDLE},
        RuntimeFailureCase{
            ApiCall::WaitForRender,
            1,
            AudioFailureStage::WaitRenderEvent,
            E_ABORT},
        RuntimeFailureCase{
            ApiCall::GetClockPosition,
            0,
            AudioFailureStage::GetClockPosition,
            AUDCLNT_E_DEVICE_INVALIDATED},
        RuntimeFailureCase{
            ApiCall::GetClockPosition,
            0,
            AudioFailureStage::GetClockPosition,
            E_FAIL,
            true},
    };
    int failures = 0;
    for (const auto& test : cases) {
        EngineFixture fixture;
        if (test.call == ApiCall::GetRenderBuffer ||
            test.call == ApiCall::ReleaseRenderBuffer) {
            fixture.api->failure_call = test.call;
            fixture.api->failure_result = test.result;
            fixture.api->failure_occurrence = test.occurrence;
        }
        auto engine = fixture.Start();
        failures += Expect(engine != nullptr, "runtime case starts engine");
        if (test.current_output_frame) {
            fixture.api->PushClock(test.result);
            failures += Expect(
                !engine->CurrentOutputFrame().has_value(),
                "CurrentOutputFrame propagates endpoint failure");
        } else if (test.call == ApiCall::WaitForRender) {
            fixture.api->PushWait(test.result);
        } else {
            fixture.api->PushClock(
                test.call == ApiCall::GetClockPosition
                    ? test.result
                    : S_OK,
                kInitialClock + kPeriod,
                kInitialClock + kPeriod);
            fixture.api->PushWait();
        }
        failures += Expect(WaitForFatal(fixture), "runtime case fatal callback");
        failures += Expect(
            fixture.observer_state->fatal_failure.stage == test.stage &&
                fixture.observer_state->fatal_failure.result == test.result,
            "exact first runtime stage and HRESULT");
        failures += Expect(
            fixture.observer_state->fatal_counters.endpoint_hresult_failures ==
                1,
            "one endpoint failure counted");
        engine.reset();
        failures += ExpectOwnerThreadCleanup(fixture, "runtime-case audio thread");
    }
    return failures;
}

int TestExplicitTestShutdown() {
    int failures = 0;
    EngineFixture fixture;
    auto engine = fixture.Start();
    failures += Expect(engine != nullptr, "shutdown case starts engine");
    engine.reset();
    failures += Expect(
        fixture.observer_state->fatal_count.load() == 0,
        "explicit test shutdown emits no fatal callback");
    failures += ExpectOwnerThreadCleanup(fixture, "explicit shutdown audio thread");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestBoundedInitializationTimeout();
    failures += TestStartupFailuresAndStartOrdering();
    failures += TestSilenceRuntimeFailureAndCleanup();
    failures += TestVoiceClockSummaryAndRenderSafety();
    failures += TestExactRuntimeFailureStages();
    failures += TestExplicitTestShutdown();
    if (failures == 0) {
        std::cout << "ExclusiveAudioEngineTests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
