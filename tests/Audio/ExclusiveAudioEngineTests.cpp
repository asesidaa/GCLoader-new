#include "Audio/Wasapi/ExclusiveAudioEngine.h"
#include "Audio/Wasapi/ExclusiveAudioEngineInternal.h"
#include "Audio/Diagnostics/AudioFlightRecorder.h"

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
#include <type_traits>

namespace {

using namespace std::chrono_literals;
using gc::audio::AudioCursorTimeline;
using gc::audio::AudioCursorResolutionKind;
using gc::audio::AudioFailure;
using gc::audio::AudioFailureStage;
using gc::audio::AudioLockRegions;
using gc::audio::AudioRuntimeCountersSnapshot;
using gc::audio::AudioSnapshot;
using gc::audio::AudioStartupFailure;
using gc::audio::EndpointFormatKind;
using gc::audio::EndpointInitialization;
using gc::audio::EndpointPcmFormat;
using gc::audio::ExclusiveAudioEngine;
using gc::audio::IAudioEngineObserver;
using gc::audio::IWasapiApi;
using gc::audio::NormalizedSourceFormat;
using gc::audio::VoiceUsage;

static_assert(std::is_nothrow_move_constructible_v<EndpointInitialization>);
static_assert(std::is_nothrow_move_assignable_v<EndpointInitialization>);
static_assert(std::is_nothrow_move_constructible_v<AudioStartupFailure>);
static_assert(std::is_nothrow_move_assignable_v<AudioStartupFailure>);

constexpr std::uint32_t kFrames = 8;
constexpr std::size_t kSamples = kFrames * gc::audio::kOutputChannels;
constexpr std::uint32_t kDiagnosticFrames = 480;
constexpr std::size_t kDiagnosticSamples =
    kDiagnosticFrames * gc::audio::kOutputChannels;
constexpr std::uint64_t kClockFrequency = 10'000'000;
constexpr std::uint64_t kInitialClock = 1'000;
constexpr auto kPeriod = static_cast<REFERENCE_TIME>(1'814);
constexpr HRESULT kWaitFailure = HRESULT_FROM_WIN32(ERROR_TIMEOUT);

constexpr std::uint64_t ClockPositionForOutputFrame(
    std::uint64_t frame,
    std::uint32_t output_sample_rate =
        gc::audio::kGamePrimarySampleRate) noexcept {
    return kInitialClock +
        (frame * kClockFrequency + output_sample_rate - 1) /
            output_sample_rate;
}

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

int TestProductionRenderFinalizationAndStartupTimeoutClamp() {
    int failures = 0;
    failures += Expect(
        gc::audio::detail::ClampExclusiveAudioStartupTimeout(10'001) ==
                10'000 &&
            gc::audio::detail::ClampExclusiveAudioStartupTimeout(10'000) ==
                10'000 &&
            gc::audio::detail::ClampExclusiveAudioStartupTimeout(0) == 0,
        "startup timeout is capped at ten seconds");
    failures += Expect(
        gc::audio::detail::ExclusiveAudioEngineTiming{}
                .summary_interval_ms == 30'000,
        "production runtime summary interval defaults to thirty seconds");

    std::array<float, kSamples> short_block{};
    short_block.fill(0.5F);
    failures += Expect(
        gc::audio::detail::FinalizeMixerRenderBlock(
            short_block,
            kFrames,
            {MA_SUCCESS, kFrames / 2}),
        "successful short mixer read reports silence fallback");
    failures += Expect(
        std::all_of(
            short_block.begin(),
            short_block.begin() +
                static_cast<std::ptrdiff_t>(kSamples / 2),
            [](float sample) { return sample == 0.5F; }) &&
            std::all_of(
                short_block.begin() +
                    static_cast<std::ptrdiff_t>(kSamples / 2),
                short_block.end(),
                [](float sample) { return sample == 0.0F; }),
        "successful short mixer read zero-fills only the missing suffix");

    std::array<float, kSamples> failed_block{};
    failed_block.fill(0.5F);
    failures += Expect(
        gc::audio::detail::FinalizeMixerRenderBlock(
            failed_block,
            kFrames,
            {MA_ERROR, 0}) &&
            std::all_of(
                failed_block.begin(),
                failed_block.end(),
                [](float sample) { return sample == 0.0F; }),
        "mixer error zero-fills the complete production block");
    return failures;
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
    struct LifetimeState {
        std::atomic_bool caller_owners_dropped{};
        std::atomic_bool owner_destroyed{};
        std::atomic_uint64_t callbacks_after_caller_drop{};
        std::atomic_uint64_t callbacks_after_owner_release{};
    };

    std::shared_ptr<LifetimeState> lifetime;
    std::atomic_bool enabled{};
    std::atomic_uint64_t active_callbacks{};
    std::atomic_uint64_t lifetime_callbacks{};
    std::atomic_bool fail_allocations{};

    void RecordCallback() noexcept {
        lifetime_callbacks.fetch_add(1, std::memory_order_relaxed);
        if (lifetime != nullptr &&
            lifetime->caller_owners_dropped.load(std::memory_order_acquire)) {
            lifetime->callbacks_after_caller_drop.fetch_add(
                1, std::memory_order_relaxed);
        }
        if (lifetime != nullptr &&
            lifetime->owner_destroyed.load(std::memory_order_acquire)) {
            lifetime->callbacks_after_owner_release.fetch_add(
                1, std::memory_order_relaxed);
        }
        if (enabled.load(std::memory_order_relaxed)) {
            active_callbacks.fetch_add(1, std::memory_order_relaxed);
        }
    }

    static void* Allocate(std::size_t size, void* user_data) {
        auto& probe = *static_cast<AllocationProbe*>(user_data);
        probe.RecordCallback();
        if (probe.fail_allocations.load(std::memory_order_relaxed)) {
            return nullptr;
        }
        return std::malloc(size == 0 ? 1 : size);
    }

    static void* Reallocate(
        void* pointer, std::size_t size, void* user_data) {
        auto& probe = *static_cast<AllocationProbe*>(user_data);
        probe.RecordCallback();
        if (probe.fail_allocations.load(std::memory_order_relaxed)) {
            return nullptr;
        }
        return std::realloc(pointer, size == 0 ? 1 : size);
    }

    static void Free(void* pointer, void* user_data) {
        auto& probe = *static_cast<AllocationProbe*>(user_data);
        probe.RecordCallback();
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

struct AllocationOwner {
    explicit AllocationOwner(
        std::shared_ptr<AllocationProbe::LifetimeState> lifetime_state)
        : probe{.lifetime = std::move(lifetime_state)},
          callbacks(probe.Callbacks()) {}

    ~AllocationOwner() {
        probe.lifetime->owner_destroyed.store(true, std::memory_order_release);
    }

    AllocationProbe probe;
    ma_allocation_callbacks callbacks{};
};

enum class ApiCall : std::uint8_t {
    InitializeComMta,
    OpenDefaultConsoleEndpoint,
    ActivateAudioClient,
    IsExactFormatSupported,
    GetDevicePeriod,
    InitializeExclusiveEvent,
    GetBufferSize,
    GetStreamLatency,
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

struct CallSlot {
    CallRecord record{};
    std::atomic_bool ready{};
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
    std::array<CallSlot, 256> calls{};
    std::atomic_size_t call_count{};
    std::array<WaitAction, 64> waits{};
    std::size_t wait_read{};
    std::size_t wait_write{};
    std::array<ClockAction, 64> clocks{};
    std::size_t clock_read{};
    std::size_t clock_write{};
    std::array<std::array<std::int16_t, kDiagnosticSamples>, 32> submissions{};
    std::atomic_size_t submission_count{};
    std::array<
        std::byte,
        kDiagnosticSamples * sizeof(std::int16_t)> render_bytes{};
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
    std::wstring endpoint_name{L"Fake Speakers"};
    std::wstring endpoint_id{L"fake-endpoint-id"};
    std::uint32_t supported_output_rate{
        gc::audio::kGamePrimarySampleRate};
    std::uint32_t buffer_frames{kFrames};
    EndpointFormatKind supported_format_kind{
        EndpointFormatKind::LegacyPcm};
    std::array<EndpointPcmFormat, 4> probed_formats{};
    std::uint32_t format_probe_count{};

    void Record(ApiCall call) noexcept {
        const auto index = call_count.fetch_add(1, std::memory_order_relaxed);
        if (index < calls.size()) {
            calls[index].record = {call, GetCurrentThreadId()};
            calls[index].ready.store(true, std::memory_order_release);
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
            found += calls[index].ready.load(std::memory_order_acquire) &&
                    calls[index].record.call == wanted
                ? 1
                : 0;
        }
        return found;
    }

    std::size_t First(ApiCall wanted) const noexcept {
        const auto count = std::min(call_count.load(), calls.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (calls[index].ready.load(std::memory_order_acquire) &&
                calls[index].record.call == wanted) {
                return index;
            }
        }
        return calls.size();
    }
};

class FakeAudioDiagnosticSink final
    : public gc::audio::diagnostics::IAudioDiagnosticSink {
public:
    explicit FakeAudioDiagnosticSink(
        std::shared_ptr<FakeWasapiState> api) noexcept
        : api_(std::move(api)) {}

    bool StartSession(
        const gc::audio::diagnostics::AudioFlightRecorderSession& session)
        noexcept override {
        last_session = session;
        start_before_endpoint.store(
            !api_->started.load(std::memory_order_acquire) &&
                api_->First(ApiCall::Start) == api_->calls.size(),
            std::memory_order_relaxed);
        start_seen.store(true, std::memory_order_release);
        return start_result;
    }

    void PublishEvent(
        gc::audio::diagnostics::AudioDiagnosticEvent) noexcept override {}

    gc::audio::diagnostics::PcmPublishResult PublishSubmittedPcm(
        const gc::audio::diagnostics::SubmittedPcmMetadata& metadata,
        std::span<const std::int16_t> samples) noexcept override {
        last_metadata = metadata;
        sample_count = std::min(samples.size(), last_samples.size());
        std::copy_n(samples.begin(), sample_count, last_samples.begin());
        release_count_at_publish = api_->Count(ApiCall::ReleaseRenderBuffer);
        publish_inside_render_probe.store(
            api_->render_probe.load(std::memory_order_acquire),
            std::memory_order_relaxed);
        allocation_callbacks_at_publish =
            api_->allocations == nullptr
            ? 0
            : api_->allocations->active_callbacks.load(
                  std::memory_order_relaxed);
        const auto call =
            pcm_calls.fetch_add(1, std::memory_order_release) + 1;
        return {call, true};
    }

    std::array<std::int16_t, kDiagnosticSamples> last_samples{};
    gc::audio::diagnostics::AudioFlightRecorderSession last_session{};
    gc::audio::diagnostics::SubmittedPcmMetadata last_metadata{};
    std::size_t sample_count{};
    std::size_t release_count_at_publish{};
    std::uint64_t allocation_callbacks_at_publish{};
    std::atomic_uint32_t pcm_calls{};
    std::atomic_bool start_seen{};
    std::atomic_bool start_before_endpoint{};
    std::atomic_bool publish_inside_render_probe{};
    bool start_result{true};

private:
    std::shared_ptr<FakeWasapiState> api_;
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
        *name = state_->endpoint_name;
        *id = state_->endpoint_id;
        return state_->MaybeFail(ApiCall::OpenDefaultConsoleEndpoint);
    }

    HRESULT ActivateAudioClient() noexcept override {
        state_->Record(ApiCall::ActivateAudioClient);
        return state_->MaybeFail(ApiCall::ActivateAudioClient);
    }

    HRESULT IsExactFormatSupported(
        const EndpointPcmFormat& format) noexcept override {
        state_->Record(ApiCall::IsExactFormatSupported);
        if (state_->format_probe_count < state_->probed_formats.size()) {
            state_->probed_formats[state_->format_probe_count] = format;
        }
        ++state_->format_probe_count;
        const auto injected = state_->MaybeFail(
            ApiCall::IsExactFormatSupported);
        if (injected != S_OK) {
            return injected;
        }
        return format.wave_format().nSamplesPerSec ==
                    state_->supported_output_rate &&
                format.kind == state_->supported_format_kind
            ? S_OK
            : AUDCLNT_E_UNSUPPORTED_FORMAT;
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
        const gc::audio::EndpointPcmFormat&) noexcept override {
        state_->Record(ApiCall::InitializeExclusiveEvent);
        return state_->MaybeFail(ApiCall::InitializeExclusiveEvent);
    }

    HRESULT GetBufferSize(std::uint32_t* frames) noexcept override {
        state_->Record(ApiCall::GetBufferSize);
        *frames = state_->buffer_frames;
        return state_->MaybeFail(ApiCall::GetBufferSize);
    }

    HRESULT GetStreamLatency(REFERENCE_TIME* latency) noexcept override {
        state_->Record(ApiCall::GetStreamLatency);
        const auto failure = state_->MaybeFail(ApiCall::GetStreamLatency);
        if (SUCCEEDED(failure)) {
            *latency = kPeriod;
        }
        return failure;
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
        if (frames != state_->buffer_frames) {
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
        if (frames != state_->buffer_frames) {
            return E_INVALIDARG;
        }
        if (state_->started.load() && flags == 0) {
            const auto index = state_->submission_count.load(
                std::memory_order_relaxed);
            if (index < state_->submissions.size()) {
                std::memcpy(
                    state_->submissions[index].data(),
                    state_->render_bytes.data(),
                    static_cast<std::size_t>(frames) *
                        gc::audio::kOutputChannels *
                        sizeof(std::int16_t));
                state_->submission_count.store(
                    index + 1, std::memory_order_release);
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
        const auto state = state_;
        state->Record(ApiCall::GetClockPosition);
        ClockAction action{};
        {
            std::lock_guard lock(state->mutex);
            if (state->clock_read != state->clock_write) {
                action = state->clocks[
                    state->clock_read++ % state->clocks.size()];
            }
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
    std::mutex startup_barrier_mutex;
    std::condition_variable startup_barrier_condition;
    std::atomic_bool block_startup{};
    std::atomic_bool startup_entered{};
    std::atomic_bool release_startup{};
    std::atomic_bool startup_completed{};
    std::atomic_uint32_t callback_sequence{};
    std::atomic_uint32_t startup_completed_sequence{};
    std::atomic_uint32_t fatal_sequence{};
};

class FakeObserver final : public IAudioEngineObserver {
public:
    explicit FakeObserver(std::shared_ptr<ObserverState> state) noexcept
        : state_(std::move(state)) {}

    void StartupSucceeded(
        const EndpointInitialization& initialization) noexcept override {
        CallbackProbe();
        if (state_->block_startup.load(std::memory_order_acquire)) {
            std::unique_lock lock(state_->startup_barrier_mutex);
            state_->startup_entered.store(true, std::memory_order_release);
            state_->startup_barrier_condition.notify_all();
            state_->startup_barrier_condition.wait(lock, [&] {
                return state_->release_startup.load(
                    std::memory_order_acquire);
            });
        }
        {
            std::lock_guard lock(state_->mutex);
            state_->startup = initialization;
            state_->startup_thread_id.store(GetCurrentThreadId());
            state_->startup_count.fetch_add(1);
        }
        state_->startup_completed_sequence.store(
            state_->callback_sequence.fetch_add(1) + 1,
            std::memory_order_release);
        state_->startup_completed.store(true, std::memory_order_release);
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
        const auto sequence = state_->callback_sequence.fetch_add(1) + 1;
        {
            std::lock_guard lock(state_->mutex);
            state_->fatal_failure = failure;
            state_->fatal_counters = counters;
            state_->fatal_thread_id.store(GetCurrentThreadId());
            state_->fatal_sequence.store(
                sequence, std::memory_order_relaxed);
        }
        state_->fatal_count.fetch_add(1, std::memory_order_release);
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
    std::shared_ptr<AllocationProbe::LifetimeState> allocation_lifetime =
        std::make_shared<AllocationProbe::LifetimeState>();
    std::shared_ptr<AllocationOwner> allocation_owner =
        std::make_shared<AllocationOwner>(allocation_lifetime);
    std::shared_ptr<const ma_allocation_callbacks> callbacks{
        allocation_owner, &allocation_owner->callbacks};
    gc::audio::detail::ExclusiveAudioEngineTiming timing{
        .summary_interval_ms = 250,
    };

    EngineFixture() {
        api->allocations = &allocation_owner->probe;
        observer_state->render_probe = &api->render_probe;
        api->PushClock(S_OK, kInitialClock, kInitialClock);
    }

    std::unique_ptr<ExclusiveAudioEngine> Start(
        AudioStartupFailure* failure = nullptr,
        DWORD timeout_ms = 2'000,
        REFERENCE_TIME configured_duration = kPeriod,
        gc::audio::diagnostics::IAudioDiagnosticSink* diagnostic_sink =
            nullptr) {
        return gc::audio::detail::StartExclusiveAudioEngineAndWait(
            std::make_unique<FakeWasapiApi>(api),
            observer,
            timeout_ms,
            configured_duration,
            callbacks,
            timing,
            diagnostic_sink,
            failure);
    }
};

WAVEFORMATEX Pcm16Mono() noexcept {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = gc::audio::kGamePrimarySampleRate;
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

int TestRecorderStartsBeforeEndpointRendering() {
    int failures = 0;
    EngineFixture fixture;
    fixture.api->supported_output_rate = 48'000;
    fixture.api->buffer_frames = kDiagnosticFrames;
    FakeAudioDiagnosticSink sink(fixture.api);

    auto engine = fixture.Start(nullptr, 2'000, 100'000, &sink);
    failures += Expect(engine != nullptr, "diagnostic engine startup");
    failures += Expect(
        sink.start_seen.load(std::memory_order_acquire) &&
            sink.start_before_endpoint.load(std::memory_order_relaxed),
        "diagnostic session starts before endpoint rendering");
    failures += Expect(
        sink.last_session.sample_rate == 48'000 &&
            sink.last_session.channels == gc::audio::kOutputChannels &&
            sink.last_session.bits_per_sample ==
                gc::audio::kOutputBitsPerSample &&
            sink.last_session.frames_per_block == kDiagnosticFrames &&
            sink.last_session.qpc_frequency != 0,
        "diagnostic session uses negotiated endpoint geometry");

    engine.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "diagnostic startup audio thread");
    return failures;
}

int TestRecorderStartFailureDoesNotFailAudioEngine() {
    int failures = 0;
    EngineFixture fixture;
    FakeAudioDiagnosticSink sink(fixture.api);
    sink.start_result = false;
    AudioStartupFailure startup_failure{};

    auto engine = fixture.Start(
        &startup_failure, 2'000, kPeriod, &sink);
    failures += Expect(
        engine != nullptr &&
            sink.start_seen.load(std::memory_order_acquire) &&
            fixture.api->started.load(std::memory_order_acquire),
        "diagnostic startup failure leaves audio engine active");
    failures += Expect(
        startup_failure.failure.stage == AudioFailureStage::None,
        "diagnostic startup failure does not publish audio failure");

    engine.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "diagnostic failure audio thread");
    return failures;
}

int TestSuccessfulSubmissionPublishesExactPcmAndTimeline() {
    int failures = 0;
    EngineFixture fixture;
    fixture.api->supported_output_rate = 48'000;
    fixture.api->buffer_frames = kDiagnosticFrames;
    FakeAudioDiagnosticSink sink(fixture.api);
    auto engine = fixture.Start(nullptr, 2'000, 100'000, &sink);
    failures += Expect(engine != nullptr, "captured render engine startup");

    auto source = MakeConstantSource(failures);
    ma_result voice_result = MA_ERROR;
    auto voice = engine->CreateVoice(
        source.format,
        source.snapshot,
        source.timeline,
        VoiceUsage::GameplayNativeCandidate,
        &voice_result);
    failures += Expect(
        voice != nullptr && voice_result == MA_SUCCESS &&
            voice->Play(true, 1) == DS_OK,
        "captured render voice starts");

    constexpr std::uint64_t qpc_100ns = 987'654;
    const auto clock_position =
        ClockPositionForOutputFrame(kDiagnosticFrames, 48'000);
    fixture.allocation_owner->probe.Begin();
    fixture.api->render_probe.store(true, std::memory_order_release);
    fixture.api->PushClock(clock_position, qpc_100ns);
    fixture.api->PushWait();
    failures += Expect(
        WaitUntil([&] {
            return sink.pcm_calls.load(std::memory_order_acquire) == 1;
        }),
        "one submitted endpoint block reaches diagnostic sink");
    fixture.api->render_probe.store(false, std::memory_order_release);
    const auto allocation_callbacks =
        fixture.allocation_owner->probe.End();

    failures += Expect(
        sink.sample_count == kDiagnosticSamples &&
            fixture.api->submission_count.load(std::memory_order_acquire) ==
                1 &&
            std::equal(
                sink.last_samples.begin(),
                sink.last_samples.end(),
                fixture.api->submissions[0].begin()),
        "diagnostic sink receives byte-identical 960-sample PCM");
    failures += Expect(
        sink.last_metadata.endpoint_clock_position == clock_position &&
            sink.last_metadata.endpoint_qpc_100ns == qpc_100ns &&
            sink.last_metadata.presented_output_frame ==
                kDiagnosticFrames &&
            sink.last_metadata.output_frame_begin == kDiagnosticFrames &&
            sink.last_metadata.submitted_tail ==
                2 * kDiagnosticFrames &&
            sink.last_metadata.discontinuity_frames == 0 &&
            sink.last_metadata.mixer_frames_read == kDiagnosticFrames &&
            sink.last_metadata.mixer_result == MA_SUCCESS &&
            sink.last_metadata.pacing_kind == static_cast<std::uint8_t>(
                gc::audio::OutputPacingDecisionKind::Sequential),
        "diagnostic metadata preserves endpoint and pacing timeline");
    failures += Expect(
        sink.release_count_at_publish == 2,
        "successful ReleaseRenderBuffer precedes diagnostic publication");
    failures += Expect(
        sink.publish_inside_render_probe.load(std::memory_order_relaxed) &&
            sink.allocation_callbacks_at_publish == 0 &&
            allocation_callbacks == 0,
        "diagnostic publication performs no mixer allocation callback");

    voice->Stop();
    engine.reset();
    voice.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "captured render audio thread");
    return failures;
}

int TestReleaseBufferFailurePublishesNoSubmittedPcm() {
    int failures = 0;
    EngineFixture fixture;
    FakeAudioDiagnosticSink sink(fixture.api);
    fixture.api->failure_call = ApiCall::ReleaseRenderBuffer;
    fixture.api->failure_occurrence = 2;
    fixture.api->failure_result = E_HANDLE;
    auto engine = fixture.Start(nullptr, 2'000, kPeriod, &sink);
    failures += Expect(engine != nullptr, "failed-release engine startup");

    fixture.api->PushClock(
        ClockPositionForOutputFrame(kFrames),
        kInitialClock + kPeriod);
    fixture.api->PushWait();
    failures += Expect(
        WaitForFatal(fixture) &&
            fixture.observer_state->fatal_failure.stage ==
                AudioFailureStage::ReleaseRenderBuffer,
        "failed endpoint release reaches runtime failure");
    failures += Expect(
        sink.pcm_calls.load(std::memory_order_acquire) == 0,
        "failed endpoint release publishes no diagnostic PCM");

    engine.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "failed-release diagnostic audio thread");
    return failures;
}

int TestNullDiagnosticSinkPreservesEngineBehavior() {
    int failures = 0;
    EngineFixture fixture;
    auto engine = fixture.Start(nullptr, 2'000, kPeriod, nullptr);
    failures += Expect(
        engine != nullptr && fixture.api->started.load(),
        "null diagnostic sink preserves successful startup");

    fixture.api->PushClock(
        ClockPositionForOutputFrame(kFrames),
        kInitialClock + kPeriod);
    fixture.api->PushWait();
    failures += Expect(
        WaitUntil([&] {
            return fixture.api->submission_count.load(
                       std::memory_order_acquire) == 1;
        }),
        "null diagnostic sink preserves endpoint submission");

    engine.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "null diagnostic sink audio thread");
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
    const std::weak_ptr<AllocationOwner> weak_allocations =
        fixture.allocation_owner;
    fixture.callbacks.reset();
    fixture.allocation_owner.reset();
    fixture.allocation_lifetime->caller_owners_dropped.store(
        true, std::memory_order_release);
    failures += Expect(
        !weak_allocations.expired() &&
            !fixture.allocation_lifetime->owner_destroyed.load(
                std::memory_order_acquire),
        "timed-out engine owns allocator callbacks and user state");
    fixture.api->release_initialize.store(true);
    fixture.api->condition.notify_all();
    failures += Expect(
        WaitUntil([&] { return fixture.api->destroy_calls.load() == 1; }),
        "abandoned timeout object eventually performs owner-thread cleanup");
    failures += Expect(
        fixture.allocation_lifetime->callbacks_after_caller_drop.load(
            std::memory_order_acquire) != 0 &&
            !fixture.allocation_lifetime->owner_destroyed.load(
                std::memory_order_acquire),
        "abandoned initialization safely uses retained allocator state");
    return failures;
}

int TestAllocatorOwnerOutlivesEngineThroughVoiceDestruction() {
    int failures = 0;
    EngineFixture fixture;
    fixture.callbacks.reset();
    fixture.allocation_owner.reset();

    auto lifetime = std::make_shared<AllocationProbe::LifetimeState>();
    auto* retained_owner = new AllocationOwner(lifetime);
    fixture.allocation_lifetime = lifetime;
    fixture.allocation_owner = std::shared_ptr<AllocationOwner>(
        retained_owner,
        [lifetime](AllocationOwner*) {
            lifetime->owner_destroyed.store(true, std::memory_order_release);
        });
    fixture.callbacks = {
        fixture.allocation_owner, &retained_owner->callbacks};
    fixture.api->allocations = &retained_owner->probe;

    auto engine = fixture.Start();
    failures += Expect(engine != nullptr, "allocator-state engine startup");
    auto source = MakeConstantSource(failures);
    ma_result voice_result = MA_ERROR;
    auto voice = engine->CreateVoice(
        source.format,
        source.snapshot,
        source.timeline,
        VoiceUsage::General,
        &voice_result);
    failures += Expect(
        voice != nullptr && voice_result == MA_SUCCESS,
        "allocator-state voice creation");

    fixture.callbacks.reset();
    fixture.allocation_owner.reset();
    lifetime->caller_owners_dropped.store(true, std::memory_order_release);
    const auto callbacks_before_engine_destruction =
        retained_owner->probe.lifetime_callbacks.load(
            std::memory_order_acquire);
    engine.reset();

    failures += Expect(
        !lifetime->owner_destroyed.load(std::memory_order_acquire) &&
            retained_owner->probe.lifetime_callbacks.load(
                std::memory_order_acquire) ==
                callbacks_before_engine_destruction,
        "voice-retained mixer state keeps allocator owner after engine destruction");

    voice.reset();
    failures += Expect(
        lifetime->owner_destroyed.load(std::memory_order_acquire) &&
            lifetime->callbacks_after_caller_drop.load(
                std::memory_order_acquire) != 0 &&
            lifetime->callbacks_after_owner_release.load(
                std::memory_order_acquire) == 0,
        "voice teardown frees node converter and engine before allocator release");
    delete retained_owner;
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
    {
        EngineFixture fixture;
        fixture.api->endpoint_name.assign(2'048, L'N');
        fixture.api->endpoint_id.assign(2'048, L'I');
        fixture.allocation_owner->probe.fail_allocations.store(true);
        AudioStartupFailure failure{};
        auto engine = fixture.Start(&failure);
        failures += Expect(engine == nullptr, "post-identity mixer failure");
        failures += Expect(
            failure.failure.stage == AudioFailureStage::InitializeMixer &&
                failure.failure.result == E_OUTOFMEMORY &&
                failure.attempted.endpoint_name == fixture.api->endpoint_name &&
                failure.attempted.endpoint_id == fixture.api->endpoint_id,
            "long endpoint metadata moves through allocation failure");
        failures += ExpectOwnerThreadCleanup(
            fixture, "long metadata failure audio thread");
    }
    return failures;
}

int TestStartupCallbackPrecedesImmediateRuntimeFailure() {
    int failures = 0;
    EngineFixture fixture;
    fixture.observer_state->block_startup.store(true);
    fixture.api->PushWait(E_ABORT);
    AudioStartupFailure failure{};
    std::unique_ptr<ExclusiveAudioEngine> engine;
    std::thread starter([&] { engine = fixture.Start(&failure); });

    failures += Expect(
        WaitUntil([&] {
            return fixture.observer_state->startup_entered.load(
                std::memory_order_acquire);
        }),
        "startup observer enters its caller-side publication barrier");
    const auto fatal_while_startup_blocked = WaitUntil(
        [&] {
            return fixture.observer_state->fatal_count.load(
                       std::memory_order_acquire) != 0;
        },
        100ms);
    failures += Expect(
        !fatal_while_startup_blocked,
        "immediate runtime fatal waits for startup publication");

    fixture.observer_state->release_startup.store(
        true, std::memory_order_release);
    fixture.observer_state->startup_barrier_condition.notify_all();
    starter.join();
    failures += Expect(engine != nullptr, "startup returns the initialized engine");
    failures += Expect(
        WaitForFatal(fixture),
        "immediate runtime failure publishes after startup");
    failures += Expect(
        fixture.observer_state->startup_completed_sequence.load(
                std::memory_order_acquire) != 0 &&
            fixture.observer_state->startup_completed_sequence.load(
                std::memory_order_acquire) <
                fixture.observer_state->fatal_sequence.load(
                    std::memory_order_acquire),
        "startup observer completion precedes fatal observer entry");
    engine.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "startup ordering audio thread");
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
        WaitUntil([&] {
            return fixture.api->submission_count.load(
                       std::memory_order_acquire) == 3;
        }),
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

int TestPreStartClockOriginAndRecoverableGapPacing() {
    int failures = 0;
    EngineFixture fixture;
    auto engine = fixture.Start();
    failures += Expect(engine != nullptr, "paced engine startup");
    failures += Expect(
        fixture.api->First(ApiCall::GetClockPosition) <
            fixture.api->First(ApiCall::Start),
        "stopped stream clock is sampled before endpoint Start");

    auto source = MakeConstantSource(failures);
    ma_result voice_result = MA_ERROR;
    auto voice = engine->CreateVoice(
        source.format,
        source.snapshot,
        source.timeline,
        VoiceUsage::GameplayNativeCandidate,
        &voice_result);
    failures += Expect(
        voice != nullptr && voice_result == MA_SUCCESS &&
            voice->Play(true, 1) == DS_OK,
        "paced test voice starts");

    constexpr std::uint64_t first_qpc = 0;
    fixture.api->PushClock(ClockPositionForOutputFrame(8), first_qpc);
    fixture.api->PushWait();
    failures += Expect(
        WaitUntil([&] {
            return fixture.api->submission_count.load(
                       std::memory_order_acquire) >= 1;
        }) &&
            source.timeline->ResolveSourceFrame(8, 1, kFrames).kind ==
                AudioCursorResolutionKind::Resolved &&
            source.timeline->ResolveSourceFrame(0, 1, kFrames).kind ==
                AudioCursorResolutionKind::PendingGeneration,
        "first post-start event renders the packet after silent prefill");

    fixture.api->PushClock(
        ClockPositionForOutputFrame(16),
        first_qpc + static_cast<std::uint64_t>(kPeriod + kPeriod / 2 + 1));
    fixture.api->PushWait();
    failures += Expect(
        WaitUntil([&] {
            return fixture.api->submission_count.load(
                       std::memory_order_acquire) >= 2;
        }),
        "late QPC wake inside submitted tail renders sequentially");

    fixture.api->PushClock(
        ClockPositionForOutputFrame(25),
        first_qpc + static_cast<std::uint64_t>(3 * kPeriod));
    fixture.api->PushWait();
    failures += Expect(
        WaitUntil([&] {
            return fixture.api->submission_count.load(
                       std::memory_order_acquire) >= 3;
        }) &&
            source.timeline->ResolveSourceFrame(24, 1, kFrames).kind ==
                AudioCursorResolutionKind::Resolved &&
            source.timeline->ResolveSourceFrame(31, 1, kFrames).kind ==
                AudioCursorResolutionKind::Resolved &&
            source.timeline->ResolveSourceFrame(32, 1, kFrames).kind ==
                AudioCursorResolutionKind::Resolved,
        "recoverable gap advances the mixer and renders the aligned block");

    engine->CountPendingCursorQuery();
    engine->CountUnmappedCursorFailure();
    fixture.api->PushWait(E_ABORT);
    failures += Expect(WaitForFatal(fixture), "paced test reaches fatal handoff");
    const auto& counters = fixture.observer_state->fatal_counters;
    failures += Expect(
        counters.render_callbacks == 3 &&
            counters.late_event_wakes == 1 &&
            counters.confirmed_gap_events == 1 &&
            counters.skipped_output_frames == 8 &&
            counters.maximum_skipped_output_frames == 8 &&
            counters.chronic_pacing_failures == 0 &&
            counters.current_submitted_lead_frames == -1 &&
            counters.minimum_submitted_lead_frames == -1 &&
            counters.pending_cursor_queries == 1 &&
            counters.unmapped_cursor_failures == 1,
        "recoverable pacing, lead, and cursor counters remain distinct");

    voice->Stop();
    engine.reset();
    voice.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "paced recoverable-gap audio thread");
    return failures;
}

int TestChronicAndInvalidClockPacingFailures() {
    int failures = 0;
    {
        EngineFixture fixture;
        auto engine = fixture.Start();
        failures += Expect(engine != nullptr, "chronic-gap engine startup");
        constexpr std::array presentations{9ULL, 25ULL, 41ULL};
        for (std::size_t index = 0; index < presentations.size(); ++index) {
            fixture.api->PushClock(
                ClockPositionForOutputFrame(presentations[index]),
                200'000 + index * 2'000);
            fixture.api->PushWait();
            if (index != presentations.size() - 1) {
                failures += Expect(
                    WaitUntil([&] {
                        return fixture.api->submission_count.load(
                                   std::memory_order_acquire) >= index + 1;
                    }),
                    "recoverable chronic-gap prefix submits");
            }
        }
        failures += Expect(
            WaitForFatal(fixture) &&
                fixture.observer_state->fatal_failure.stage ==
                    AudioFailureStage::ChronicOutputGap &&
                fixture.observer_state->fatal_failure.result == E_FAIL,
            "third rolling gap is a chronic pacing failure");
        const auto& counters = fixture.observer_state->fatal_counters;
        failures += Expect(
            counters.render_callbacks == 2 &&
                counters.confirmed_gap_events == 3 &&
                counters.skipped_output_frames == 24 &&
                counters.maximum_skipped_output_frames == 8 &&
                counters.chronic_pacing_failures == 1 &&
                counters.endpoint_hresult_failures == 0 &&
                fixture.api->Count(ApiCall::ReleaseRenderBuffer) == 4,
            "chronic gap reports all confirmed gaps and submits one silence packet");
        engine.reset();
        failures += ExpectOwnerThreadCleanup(
            fixture, "chronic-gap audio thread");
    }
    {
        EngineFixture fixture;
        auto engine = fixture.Start();
        failures += Expect(engine != nullptr, "invalid-clock engine startup");
        fixture.api->PushClock(ClockPositionForOutputFrame(8), 300'000);
        fixture.api->PushWait();
        failures += Expect(
            WaitUntil([&] {
                return fixture.api->submission_count.load(
                           std::memory_order_acquire) == 1;
            }),
            "invalid-clock prefix submits once");
        fixture.api->PushClock(ClockPositionForOutputFrame(7), 302'000);
        fixture.api->PushWait();
        failures += Expect(
            WaitForFatal(fixture) &&
                fixture.observer_state->fatal_failure.stage ==
                    AudioFailureStage::InvalidClockPosition &&
                fixture.observer_state->fatal_counters.render_callbacks == 1 &&
                fixture.observer_state->fatal_counters.endpoint_hresult_failures ==
                    0,
            "mapped clock regression is a non-HRESULT pacing failure");
        engine.reset();
        failures += ExpectOwnerThreadCleanup(
            fixture, "invalid-clock audio thread");
    }
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
        WaitUntil([&] {
            return fixture.api->submission_count.load(
                       std::memory_order_acquire) >= 1;
        }),
        "nonzero voice render");
    failures += Expect(
        std::any_of(
            fixture.api->submissions[0].begin(),
            fixture.api->submissions[0].end(),
            [](std::int16_t sample) { return sample != 0; }),
        "voice produces nonzero PCM16");

    const auto first_rendered_frame = engine->CurrentOutputFrame();
    failures += Expect(
        first_rendered_frame == 2 * kFrames - 1 &&
            source.timeline->ResolveSourceFrame(
                *first_rendered_frame,
                1,
                kFrames).kind == AudioCursorResolutionKind::Resolved &&
            source.timeline->ResolveSourceFrame(
                *first_rendered_frame,
                1,
                kFrames).source_frame == kFrames - 1,
        "published clock projects inside the first submitted source span");

    fixture.api->PushClock(
        kInitialClock + 2 * kPeriod,
        late_qpc + kPeriod);
    fixture.api->PushWait();
    failures += Expect(
        WaitUntil([&] {
            return fixture.api->submission_count.load(
                       std::memory_order_acquire) >= 2;
        }),
        "second voice render");
    failures += Expect(
        std::any_of(
            fixture.api->submissions[1].begin(),
            fixture.api->submissions[1].end(),
            [](std::int16_t sample) { return sample != 0; }),
        "second voice render remains nonzero");

    const auto callbacks_before =
        fixture.observer_state->callback_count.load();
    fixture.allocation_owner->probe.Begin();
    fixture.api->render_probe.store(true, std::memory_order_release);
    for (std::uint64_t index = 0; index < 2; ++index) {
        fixture.api->PushClock(
            kInitialClock + (index + 3) * kPeriod,
            late_qpc + (index + 2) * kPeriod);
        fixture.api->PushWait();
    }
    failures += Expect(
        WaitUntil([&] {
            return fixture.api->submission_count.load(
                       std::memory_order_acquire) >= 4;
        }),
        "two warmed render events");
    fixture.api->render_probe.store(false, std::memory_order_release);
    failures += Expect(
        fixture.allocation_owner->probe.End() == 0,
        "zero mixer allocation/free callbacks in warmed render events");
    failures += Expect(
        fixture.observer_state->callback_count.load() == callbacks_before &&
            fixture.observer_state->render_callback_violations.load() == 0,
        "no observer callback in warmed render events");

    failures += Expect(
        source.timeline->ResolveSourceFrame(0, 1, kFrames).kind ==
                AudioCursorResolutionKind::PendingGeneration &&
            source.timeline->ResolveSourceFrame(kFrames, 1, kFrames)
                .kind == AudioCursorResolutionKind::Resolved &&
            source.timeline->ResolveSourceFrame(2 * kFrames, 1, kFrames)
                .kind == AudioCursorResolutionKind::Resolved,
        "voice timeline starts after silent prefill and advances by periods");

    const auto mapped = engine->CurrentOutputFrame();
    failures += Expect(
        mapped.has_value() && *mapped == 5 * kFrames - 1,
        "published cursor stays inside the latest submitted 44.1 kHz span");

    engine->CountPendingCursorQuery();
    engine->CountUnmappedCursorFailure();

    failures += Expect(
        WaitUntil([&] {
            std::lock_guard lock(fixture.observer_state->mutex);
            const auto& counters = fixture.observer_state->summary;
            return fixture.observer_state->summary_count.load() != 0 &&
                counters.render_callbacks >= 4 &&
                counters.late_event_wakes == 0 &&
                counters.silence_fallbacks == 0 &&
                counters.pending_cursor_queries == 1 &&
                counters.unmapped_cursor_failures == 1;
        }),
        "periodic summary includes final render counters");
    {
        std::lock_guard lock(fixture.observer_state->mutex);
        const auto& summary = fixture.observer_state->summary;
        failures += Expect(
            summary.endpoint_hresult_failures == 0,
            "cursor classifications do not increment endpoint failures");
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

int TestSelected48kEngineRate() {
    int failures = 0;
    EngineFixture fixture;
    fixture.api->supported_output_rate =
        gc::audio::kFallbackEndpointSampleRate;
    fixture.api->supported_format_kind = EndpointFormatKind::LegacyPcm;
    auto engine = fixture.Start();
    failures += Expect(
        engine != nullptr &&
            engine->output_sample_rate() ==
                gc::audio::kFallbackEndpointSampleRate &&
            fixture.observer_state->startup.has_selected_format &&
            fixture.observer_state->startup.selected_format.wave_format()
                    .nSamplesPerSec ==
                gc::audio::kFallbackEndpointSampleRate &&
            fixture.api->format_probe_count == 3,
        "engine publishes selected 48 kHz fallback");
    if (engine == nullptr) {
        return failures + 1;
    }

    auto source = MakeConstantSource(failures);
    ma_result voice_result = MA_ERROR;
    auto voice = engine->CreateVoice(
        source.format,
        source.snapshot,
        source.timeline,
        VoiceUsage::GameplayNativeCandidate,
        &voice_result);
    failures += Expect(
        voice != nullptr && voice_result == MA_SUCCESS &&
            voice->Play(true, 48) == DS_OK,
        "44.1 kHz gameplay voice starts on selected 48 kHz engine");
    if (voice == nullptr) {
        engine.reset();
        return failures + 1;
    }

    for (std::uint64_t packet = 1; packet <= 2; ++packet) {
        fixture.api->PushClock(
            ClockPositionForOutputFrame(
                packet * kFrames,
                gc::audio::kFallbackEndpointSampleRate),
            kInitialClock + packet * kPeriod);
        fixture.api->PushWait();
        failures += Expect(
            WaitUntil([&] {
                return fixture.api->submission_count.load(
                           std::memory_order_acquire) >= packet;
            }),
            "selected 48 kHz engine renders gameplay packet");
    }
    failures += Expect(
        std::any_of(
            fixture.api->submissions[1].begin(),
            fixture.api->submissions[1].end(),
            [](std::int16_t sample) { return sample != 0; }),
        "selected 48 kHz engine resamples gameplay audio");

    const auto mapped = engine->CurrentOutputFrame();
    failures += Expect(
        mapped.has_value() && *mapped == 3 * kFrames - 1,
        "published cursor stays inside the latest submitted 48 kHz span");

    fixture.api->PushWait(E_ABORT);
    failures += Expect(
        WaitForFatal(fixture) &&
            fixture.observer_state->fatal_counters.mixer
                    .native_rate_buffers == 0 &&
            fixture.observer_state->fatal_counters.mixer
                    .sample_rate_converted_buffers == 1 &&
            fixture.observer_state->fatal_counters.mixer
                    .native_gameplay_buffers == 1,
        "selected 48 kHz mixer classifies 44.1 kHz gameplay conversion");
    voice->Stop();
    engine.reset();
    voice.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "selected 48 kHz audio thread");
    return failures;
}

int TestConcurrentVoiceCreation() {
    int failures = 0;
    EngineFixture fixture;
    auto engine = fixture.Start();
    failures += Expect(engine != nullptr, "concurrent-voice engine startup");
    auto source = MakeConstantSource(failures);
    std::array<std::unique_ptr<gc::audio::MixerVoice>, 2> voices{};
    std::array<ma_result, 2> results{MA_ERROR, MA_ERROR};
    std::array<std::thread, 2> creators{
        std::thread([&] {
            voices[0] = engine->CreateVoice(
                source.format,
                source.snapshot,
                source.timeline,
                VoiceUsage::GameplayNativeCandidate,
                &results[0]);
        }),
        std::thread([&] {
            voices[1] = engine->CreateVoice(
                source.format,
                source.snapshot,
                source.timeline,
                VoiceUsage::GameplayNativeCandidate,
                &results[1]);
        })};
    for (auto& creator : creators) {
        creator.join();
    }
    failures += Expect(
        voices[0] != nullptr && voices[1] != nullptr &&
            results[0] == MA_SUCCESS && results[1] == MA_SUCCESS,
        "concurrent CreateVoice calls both publish valid voices");
    failures += Expect(
        voices[0]->Play(true, 1) == DS_OK &&
            voices[1]->Play(true, 1) == DS_OK,
        "concurrently created voices both start");
    fixture.api->PushClock(
        kInitialClock + kPeriod,
        kInitialClock + kPeriod);
    fixture.api->PushWait();
    failures += Expect(
        WaitUntil([&] {
            return fixture.api->submission_count.load(
                       std::memory_order_acquire) != 0;
        }) &&
            std::any_of(
                fixture.api->submissions[0].begin(),
                fixture.api->submissions[0].end(),
                [](std::int16_t sample) { return sample != 0; }),
        "concurrently created voices render through the shared mixer");
    for (auto& voice : voices) {
        voice->Stop();
        voice.reset();
    }
    fixture.api->PushWait(E_ABORT);
    failures += Expect(
        WaitForFatal(fixture),
        "concurrent-voice engine reaches deterministic fatal exit");
    engine.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "concurrent-voice owner audio thread");
    return failures;
}

struct RuntimeFailureCase {
    ApiCall call;
    std::uint32_t occurrence;
    AudioFailureStage stage;
    HRESULT result;
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
        if (test.call == ApiCall::WaitForRender) {
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
        if (test.call == ApiCall::ReleaseRenderBuffer) {
            failures += Expect(
                fixture.observer_state->fatal_counters.render_callbacks == 0 &&
                    fixture.api->submission_count.load(
                        std::memory_order_acquire) == 0,
                "failed release does not commit or publish the rendered tail");
        }
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

int TestConfiguredDurationPropagation() {
    int failures = 0;
    EngineFixture fixture;
    auto engine = fixture.Start(nullptr, 2'000, kPeriod);
    failures += Expect(engine != nullptr, "configured-duration engine startup");
    EndpointInitialization startup{};
    {
        std::lock_guard lock(fixture.observer_state->mutex);
        startup = fixture.observer_state->startup;
    }
    failures += Expect(
        startup.configured_duration == kPeriod &&
            startup.requested_duration == kPeriod,
        "engine forwards configured duration to endpoint and observer");
    engine.reset();
    failures += ExpectOwnerThreadCleanup(
        fixture, "configured-duration audio thread");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestProductionRenderFinalizationAndStartupTimeoutClamp();
    failures += TestRecorderStartsBeforeEndpointRendering();
    failures += TestRecorderStartFailureDoesNotFailAudioEngine();
    failures += TestSuccessfulSubmissionPublishesExactPcmAndTimeline();
    failures += TestReleaseBufferFailurePublishesNoSubmittedPcm();
    failures += TestNullDiagnosticSinkPreservesEngineBehavior();
    failures += TestBoundedInitializationTimeout();
    failures += TestAllocatorOwnerOutlivesEngineThroughVoiceDestruction();
    failures += TestStartupFailuresAndStartOrdering();
    failures += TestStartupCallbackPrecedesImmediateRuntimeFailure();
    failures += TestSilenceRuntimeFailureAndCleanup();
    failures += TestPreStartClockOriginAndRecoverableGapPacing();
    failures += TestChronicAndInvalidClockPacingFailures();
    failures += TestVoiceClockSummaryAndRenderSafety();
    failures += TestSelected48kEngineRate();
    failures += TestConcurrentVoiceCreation();
    failures += TestExactRuntimeFailureStages();
    failures += TestExplicitTestShutdown();
    failures += TestConfiguredDurationPropagation();
    if (failures == 0) {
        std::cout << "ExclusiveAudioEngineTests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
