#include "WasapiEndpoint.h"

#include <audioclient.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using gc::audio::AudioFailure;
using gc::audio::AudioFailureStage;
using gc::audio::EndpointClockPosition;
using gc::audio::EndpointInitialization;
using gc::audio::FramesToReferenceTime;
using gc::audio::IWasapiApi;
using gc::audio::WasapiEndpoint;
using gc::audio::kOutputAverageBytesPerSecond;
using gc::audio::kOutputBitsPerSample;
using gc::audio::kOutputBlockAlign;
using gc::audio::kOutputChannels;
using gc::audio::kOutputSampleRate;

enum class Call {
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
};

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

class FakeWasapiApi final : public IWasapiApi {
public:
    HRESULT InitializeComMta() noexcept override {
        initializing_thread_id = GetCurrentThreadId();
        return Record(Call::InitializeComMta);
    }

    HRESULT OpenDefaultConsoleEndpoint(
        std::wstring* name,
        std::wstring* id) noexcept override {
        const auto result = Record(Call::OpenDefaultConsoleEndpoint);
        if (SUCCEEDED(result)) {
            *name = endpoint_name;
            *id = endpoint_id;
        }
        return result;
    }

    HRESULT ActivateAudioClient() noexcept override {
        return Record(Call::ActivateAudioClient);
    }

    HRESULT IsExactFormatSupported(
        const WAVEFORMATEX& format) noexcept override {
        observed_supported_format = format;
        const auto configured = Record(
            Call::IsExactFormatSupported,
            format_result);
        return configured;
    }

    HRESULT GetDevicePeriod(
        REFERENCE_TIME* default_value,
        REFERENCE_TIME* minimum_value) noexcept override {
        const auto result = Record(Call::GetDevicePeriod);
        if (SUCCEEDED(result)) {
            *default_value = default_period;
            *minimum_value = minimum_period;
        }
        return result;
    }

    HRESULT InitializeExclusiveEvent(
        REFERENCE_TIME duration,
        REFERENCE_TIME periodicity,
        const WAVEFORMATEX& format) noexcept override {
        initialize_durations.push_back(duration);
        initialize_periodicities.push_back(periodicity);
        initialize_formats.push_back(format);
        ++initialize_count;
        const auto configured = initialize_count == 1
            ? first_initialize_result
            : retry_initialize_result;
        return Record(Call::InitializeExclusiveEvent, configured);
    }

    HRESULT GetBufferSize(std::uint32_t* frames) noexcept override {
        ++buffer_size_count;
        const auto result = Record(Call::GetBufferSize);
        if (SUCCEEDED(result)) {
            *frames = first_initialize_result ==
                    AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED &&
                    buffer_size_count == 1
                ? aligned_frames
                : actual_frames;
        }
        return result;
    }

    void ReleaseAudioClient() noexcept override {
        calls.push_back(Call::ReleaseAudioClient);
    }

    HRESULT CreateRenderEvent() noexcept override {
        return Record(Call::CreateRenderEvent);
    }

    HRESULT SetEventHandle() noexcept override {
        return Record(Call::SetEventHandle);
    }

    HRESULT GetRenderService() noexcept override {
        return Record(Call::GetRenderService);
    }

    HRESULT GetClockService() noexcept override {
        return Record(Call::GetClockService);
    }

    HRESULT GetClockFrequency(std::uint64_t* frequency) noexcept override {
        const auto result = Record(Call::GetClockFrequency);
        if (SUCCEEDED(result)) {
            *frequency = clock_frequency;
        }
        return result;
    }

    HRESULT GetRenderBuffer(
        std::uint32_t frames,
        BYTE** buffer) noexcept override {
        if (render_buffer_call_count != nullptr) {
            ++*render_buffer_call_count;
        }
        requested_render_frames.push_back(frames);
        const auto result = Record(Call::GetRenderBuffer);
        if (SUCCEEDED(result)) {
            *buffer = render_bytes.data();
        }
        return result;
    }

    HRESULT ReleaseRenderBuffer(
        std::uint32_t frames,
        DWORD flags) noexcept override {
        released_render_frames.push_back(frames);
        released_render_flags.push_back(flags);
        return Record(Call::ReleaseRenderBuffer);
    }

    HRESULT RegisterMmcssProAudio() noexcept override {
        return Record(Call::RegisterMmcssProAudio);
    }

    HRESULT SetMmcssCriticalPriority() noexcept override {
        return Record(Call::SetMmcssCriticalPriority);
    }

    HRESULT Start() noexcept override {
        return Record(Call::Start);
    }

    HRESULT WaitForRender(DWORD timeout_ms) noexcept override {
        observed_wait_timeout = timeout_ms;
        return Record(Call::WaitForRender, wait_result);
    }

    HRESULT GetClockPosition(
        std::uint64_t* position,
        std::uint64_t* qpc) noexcept override {
        const auto result = Record(Call::GetClockPosition, clock_result);
        if (!FAILED(result)) {
            *position = clock_position;
            *qpc = clock_qpc;
        }
        return result;
    }

    HRESULT ShutdownOnInitializingThread() noexcept override {
        if (shutdown_call_count != nullptr) {
            ++*shutdown_call_count;
        }
        if (GetCurrentThreadId() != initializing_thread_id) {
            if (wrong_thread_shutdown_call_count != nullptr) {
                ++*wrong_thread_shutdown_call_count;
            }
            return RPC_E_WRONG_THREAD;
        }
        return Record(Call::ShutdownOnInitializingThread);
    }

    HRESULT Record(Call call, HRESULT normal = S_OK) noexcept {
        calls.push_back(call);
        if (fail_occurrence != 0 && call == fail_call) {
            ++matching_fail_call_count;
            if (matching_fail_call_count == fail_occurrence) {
                return fail_result;
            }
        }
        return normal;
    }

    std::vector<Call> calls;
    Call fail_call{Call::InitializeComMta};
    std::uint32_t fail_occurrence{0};
    std::uint32_t matching_fail_call_count{};
    HRESULT fail_result{E_FAIL};
    HRESULT format_result{S_OK};
    HRESULT first_initialize_result{S_OK};
    HRESULT retry_initialize_result{S_OK};
    HRESULT wait_result{S_OK};
    HRESULT clock_result{S_OK};
    std::wstring endpoint_name{L"Fake Speakers"};
    std::wstring endpoint_id{L"fake-endpoint-id"};
    REFERENCE_TIME default_period{100'000};
    REFERENCE_TIME minimum_period{30'000};
    std::uint32_t aligned_frames{144};
    std::uint32_t actual_frames{static_cast<std::uint32_t>(
        gc::audio::ReferenceTimeToFramesCeil(
            minimum_period,
            kOutputSampleRate))};
    std::uint64_t clock_frequency{kOutputSampleRate};
    std::uint64_t clock_position{12'345};
    std::uint64_t clock_qpc{67'890};
    std::uint32_t initialize_count{};
    std::uint32_t buffer_size_count{};
    WAVEFORMATEX observed_supported_format{};
    std::vector<REFERENCE_TIME> initialize_durations;
    std::vector<REFERENCE_TIME> initialize_periodicities;
    std::vector<WAVEFORMATEX> initialize_formats;
    std::array<BYTE, 4096> render_bytes{};
    std::vector<std::uint32_t> requested_render_frames;
    std::vector<std::uint32_t> released_render_frames;
    std::vector<DWORD> released_render_flags;
    DWORD observed_wait_timeout{};
    std::shared_ptr<std::uint32_t> shutdown_call_count;
    std::shared_ptr<std::uint32_t> wrong_thread_shutdown_call_count;
    std::shared_ptr<std::uint32_t> render_buffer_call_count;
    DWORD initializing_thread_id{};
};

bool IsExactPcm16(const WAVEFORMATEX& format) {
    return format.wFormatTag == WAVE_FORMAT_PCM &&
        format.nChannels == kOutputChannels &&
        format.nSamplesPerSec == kOutputSampleRate &&
        format.wBitsPerSample == kOutputBitsPerSample &&
        format.nBlockAlign == kOutputBlockAlign &&
        format.nAvgBytesPerSec == kOutputAverageBytesPerSecond &&
        format.cbSize == 0;
}

int TestDirectSuccessAndRuntimeForwarding() {
    auto api = std::make_unique<FakeWasapiApi>();
    auto* observed = api.get();
    EndpointInitialization attempted{};
    AudioFailure failure{};
    auto endpoint = WasapiEndpoint::Create(
        std::move(api),
        &attempted,
        &failure);

    int failures = Expect(endpoint != nullptr, "direct endpoint creation");
    if (endpoint == nullptr) {
        return failures + 1;
    }

    const std::vector<Call> expected_initialization{
        Call::InitializeComMta,
        Call::OpenDefaultConsoleEndpoint,
        Call::ActivateAudioClient,
        Call::IsExactFormatSupported,
        Call::GetDevicePeriod,
        Call::InitializeExclusiveEvent,
        Call::GetBufferSize,
        Call::CreateRenderEvent,
        Call::SetEventHandle,
        Call::GetRenderService,
        Call::GetClockService,
        Call::GetClockFrequency,
        Call::GetRenderBuffer,
        Call::ReleaseRenderBuffer,
        Call::RegisterMmcssProAudio,
        Call::SetMmcssCriticalPriority,
    };
    failures += Expect(
        observed->calls == expected_initialization,
        "direct initialization exact call order");
    failures += Expect(
        IsExactPcm16(observed->observed_supported_format) &&
            observed->initialize_formats.size() == 1 &&
            IsExactPcm16(observed->initialize_formats.front()),
        "exact PCM16 44.1 kHz format in support and initialize calls");
    failures += Expect(
        observed->initialize_durations ==
                std::vector<REFERENCE_TIME>{observed->minimum_period} &&
            observed->initialize_periodicities ==
                std::vector<REFERENCE_TIME>{observed->minimum_period},
        "minimum period used as both direct initialize durations");
    failures += Expect(
        attempted.endpoint_name == observed->endpoint_name &&
            attempted.endpoint_id == observed->endpoint_id &&
            attempted.default_period == observed->default_period &&
            attempted.minimum_period == observed->minimum_period &&
            attempted.requested_duration == observed->minimum_period &&
            attempted.actual_buffer_frames == observed->actual_frames &&
            attempted.clock_frequency == observed->clock_frequency &&
            !attempted.alignment_retry,
        "direct attempted metadata is fully populated");
    failures += Expect(
        endpoint->initialization().actual_buffer_frames ==
                observed->actual_frames &&
            observed->requested_render_frames ==
                std::vector<std::uint32_t>{observed->actual_frames} &&
            observed->released_render_flags ==
                std::vector<DWORD>{AUDCLNT_BUFFERFLAGS_SILENT},
        "complete silent prefill before endpoint start");

    failures += Expect(
        endpoint->Start(&failure) == S_OK &&
            observed->calls.back() == Call::Start,
        "start forwards only after successful creation");

    std::vector<std::int16_t> pcm(
        static_cast<std::size_t>(observed->actual_frames) * 2);
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        pcm[index] = static_cast<std::int16_t>(index - 100);
    }
    failures += Expect(
        endpoint->SubmitPcm16(pcm, &failure) == S_OK,
        "exact-size PCM16 submission succeeds");
    failures += Expect(
        std::memcmp(
            observed->render_bytes.data(),
            pcm.data(),
            pcm.size() * sizeof(std::int16_t)) == 0 &&
            observed->released_render_frames.back() ==
                observed->actual_frames &&
            observed->released_render_flags.back() == 0,
        "PCM16 submission copies and releases without flags");

    const auto calls_before_wrong_size = observed->calls.size();
    failures += Expect(
        endpoint->SubmitPcm16(
            std::span<const std::int16_t>(pcm).first(pcm.size() - 1),
            &failure) == E_INVALIDARG &&
            observed->calls.size() == calls_before_wrong_size,
        "wrong-size PCM16 rejects before buffer acquisition");

    failures += Expect(
        endpoint->TrySubmitSilence() == S_OK &&
            observed->requested_render_frames.back() ==
                observed->actual_frames &&
            observed->released_render_flags.back() ==
                AUDCLNT_BUFFERFLAGS_SILENT,
        "runtime silence forwards one complete buffer");

    failures += Expect(
        endpoint->WaitForRender(2'000, &failure) == S_OK &&
            observed->observed_wait_timeout == 2'000,
        "render wait timeout forwards unchanged");

    observed->clock_result = S_FALSE;
    EndpointClockPosition position{};
    failures += Expect(
        endpoint->ReadClock(&position, &failure) == S_FALSE &&
            position.position == observed->clock_position &&
            position.qpc_100ns == observed->clock_qpc,
        "clock S_FALSE is accepted with returned values");
    return failures;
}

int TestAlignmentRetryUsesAuthoritativeFrames() {
    auto api = std::make_unique<FakeWasapiApi>();
    api->first_initialize_result = AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED;
    api->actual_frames = api->aligned_frames;
    auto* observed = api.get();
    EndpointInitialization attempted{};
    AudioFailure failure{};
    auto endpoint = WasapiEndpoint::Create(
        std::move(api),
        &attempted,
        &failure);

    int failures = Expect(endpoint != nullptr, "alignment retry creation");
    if (endpoint == nullptr) {
        return failures + 1;
    }
    const std::vector<Call> prefix{
        Call::InitializeComMta,
        Call::OpenDefaultConsoleEndpoint,
        Call::ActivateAudioClient,
        Call::IsExactFormatSupported,
        Call::GetDevicePeriod,
        Call::InitializeExclusiveEvent,
        Call::GetBufferSize,
        Call::ReleaseAudioClient,
        Call::ActivateAudioClient,
        Call::InitializeExclusiveEvent,
        Call::GetBufferSize,
    };
    failures += Expect(
        observed->calls.size() >= prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), observed->calls.begin()),
        "alignment retry releases and reactivates in exact order");
    failures += Expect(
        std::count(
            observed->calls.begin(),
            observed->calls.end(),
            Call::ActivateAudioClient) == 2 &&
            std::count(
                observed->calls.begin(),
                observed->calls.end(),
                Call::InitializeExclusiveEvent) == 2 &&
            std::count(
                observed->calls.begin(),
                observed->calls.end(),
                Call::ReleaseAudioClient) == 1,
        "alignment retry performs exactly two activations and initializations");
    const auto aligned_duration = FramesToReferenceTime(
        observed->aligned_frames,
        kOutputSampleRate);
    failures += Expect(
        observed->initialize_durations ==
                std::vector<REFERENCE_TIME>{
                    observed->minimum_period,
                    aligned_duration} &&
            observed->initialize_periodicities ==
                observed->initialize_durations &&
            observed->initialize_formats.size() == 2 &&
            IsExactPcm16(observed->initialize_formats[0]) &&
            IsExactPcm16(observed->initialize_formats[1]),
        "alignment retry duration and exact format");
    failures += Expect(
        attempted.alignment_retry &&
            attempted.requested_duration == aligned_duration &&
            attempted.actual_buffer_frames == observed->aligned_frames,
        "alignment retry metadata uses authoritative aligned frames");
    return failures;
}

template <typename Configure>
int ExpectCreateFailure(
    Configure configure,
    AudioFailureStage expected_stage,
    HRESULT expected_result,
    std::string_view name) {
    auto api = std::make_unique<FakeWasapiApi>();
    configure(*api);
    EndpointInitialization attempted{};
    AudioFailure failure{};
    auto endpoint = WasapiEndpoint::Create(
        std::move(api),
        &attempted,
        &failure);
    return Expect(
        endpoint == nullptr && failure.stage == expected_stage &&
            failure.result == expected_result,
        name);
}

int TestInitializationRejectionsAndStages() {
    int failures = 0;
    failures += ExpectCreateFailure(
        [](FakeWasapiApi& api) { api.actual_frames = 266; },
        AudioFailureStage::GetActualBufferSize,
        AUDCLNT_E_BUFFER_SIZE_ERROR,
        "oversized direct actual buffer rejected");
    failures += ExpectCreateFailure(
        [](FakeWasapiApi& api) {
            api.first_initialize_result = AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED;
            api.actual_frames = api.aligned_frames + 1;
        },
        AudioFailureStage::GetActualBufferSize,
        AUDCLNT_E_BUFFER_SIZE_ERROR,
        "retry actual size must equal reported aligned size");
    failures += ExpectCreateFailure(
        [](FakeWasapiApi& api) { api.format_result = S_FALSE; },
        AudioFailureStage::IsFormatSupported,
        S_FALSE,
        "closest format S_FALSE rejected");
    failures += ExpectCreateFailure(
        [](FakeWasapiApi& api) {
            api.format_result = AUDCLNT_E_UNSUPPORTED_FORMAT;
        },
        AudioFailureStage::IsFormatSupported,
        AUDCLNT_E_UNSUPPORTED_FORMAT,
        "unsupported exact format rejected");
    failures += ExpectCreateFailure(
        [](FakeWasapiApi& api) {
            api.first_initialize_result = AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED;
            api.retry_initialize_result = AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED;
        },
        AudioFailureStage::RetryInitializeExclusive,
        AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED,
        "second alignment error rejected without a third client");

    struct FailureCase {
        Call call;
        std::uint32_t occurrence;
        AudioFailureStage stage;
        std::string_view name;
    };
    const std::array cases{
        FailureCase{Call::CreateRenderEvent, 1,
                    AudioFailureStage::CreateRenderEvent, "create event stage"},
        FailureCase{Call::SetEventHandle, 1,
                    AudioFailureStage::SetEventHandle, "set event stage"},
        FailureCase{Call::GetRenderService, 1,
                    AudioFailureStage::GetRenderService, "render service stage"},
        FailureCase{Call::GetClockService, 1,
                    AudioFailureStage::GetClockService, "clock service stage"},
        FailureCase{Call::GetClockFrequency, 1,
                    AudioFailureStage::GetClockFrequency, "clock frequency stage"},
        FailureCase{Call::GetRenderBuffer, 1,
                    AudioFailureStage::PrefillGetBuffer, "prefill get stage"},
        FailureCase{Call::ReleaseRenderBuffer, 1,
                    AudioFailureStage::PrefillReleaseBuffer, "prefill release stage"},
        FailureCase{Call::RegisterMmcssProAudio, 1,
                    AudioFailureStage::RegisterMmcss, "MMCSS registration stage"},
        FailureCase{Call::SetMmcssCriticalPriority, 1,
                    AudioFailureStage::SetMmcssPriority, "MMCSS priority stage"},
    };
    for (const auto& value : cases) {
        failures += ExpectCreateFailure(
            [value](FakeWasapiApi& api) {
                api.fail_call = value.call;
                api.fail_occurrence = value.occurrence;
                api.fail_result = E_ACCESSDENIED;
            },
            value.stage,
            E_ACCESSDENIED,
            value.name);
    }
    return failures;
}

int TestSuccessfulZeroOutputsAreRejected() {
    int failures = 0;
    auto aligned_render_calls = std::make_shared<std::uint32_t>();
    failures += ExpectCreateFailure(
        [aligned_render_calls](FakeWasapiApi& api) {
            api.first_initialize_result = AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED;
            api.aligned_frames = 0;
            api.render_buffer_call_count = aligned_render_calls;
        },
        AudioFailureStage::GetAlignedBufferSize,
        AUDCLNT_E_BUFFER_SIZE_ERROR,
        "successful zero aligned frame count rejected at aligned-size stage");
    auto actual_render_calls = std::make_shared<std::uint32_t>();
    failures += ExpectCreateFailure(
        [actual_render_calls](FakeWasapiApi& api) {
            api.actual_frames = 0;
            api.render_buffer_call_count = actual_render_calls;
        },
        AudioFailureStage::GetActualBufferSize,
        AUDCLNT_E_BUFFER_SIZE_ERROR,
        "successful zero actual frame count rejected at actual-size stage");
    auto frequency_render_calls = std::make_shared<std::uint32_t>();
    failures += ExpectCreateFailure(
        [frequency_render_calls](FakeWasapiApi& api) {
            api.clock_frequency = 0;
            api.render_buffer_call_count = frequency_render_calls;
        },
        AudioFailureStage::GetClockFrequency,
        E_UNEXPECTED,
        "successful zero clock frequency rejected at clock-frequency stage");
    failures += Expect(
        *aligned_render_calls == 0 && *actual_render_calls == 0 &&
            *frequency_render_calls == 0,
        "zero successful outputs reject before render-buffer acquisition");
    return failures;
}

int TestUnaddressableOutputSizesAreRejected() {
    if constexpr (
        std::numeric_limits<std::size_t>::max() /
                kOutputChannels >=
            std::numeric_limits<std::uint32_t>::max()) {
        return 0;
    }

    const auto unaddressable_frames = static_cast<std::uint32_t>(
        std::numeric_limits<std::size_t>::max() /
            kOutputChannels +
        1);
    int failures = 0;
    {
        auto shutdown_calls = std::make_shared<std::uint32_t>();
        auto api = std::make_unique<FakeWasapiApi>();
        api->minimum_period = FramesToReferenceTime(
            unaddressable_frames, kOutputSampleRate);
        api->actual_frames = unaddressable_frames;
        api->shutdown_call_count = shutdown_calls;
        EndpointInitialization attempted{};
        AudioFailure failure{};
        auto endpoint = WasapiEndpoint::Create(
            std::move(api), &attempted, &failure);
        failures += Expect(
            endpoint == nullptr &&
                failure.stage == AudioFailureStage::GetActualBufferSize &&
                failure.result == AUDCLNT_E_BUFFER_SIZE_ERROR &&
                *shutdown_calls == 1,
            "x86 direct actual size overflow rejects with owner cleanup");
    }
    {
        auto shutdown_calls = std::make_shared<std::uint32_t>();
        auto api = std::make_unique<FakeWasapiApi>();
        api->first_initialize_result =
            AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED;
        api->aligned_frames = unaddressable_frames;
        api->actual_frames = unaddressable_frames;
        api->shutdown_call_count = shutdown_calls;
        EndpointInitialization attempted{};
        AudioFailure failure{};
        auto endpoint = WasapiEndpoint::Create(
            std::move(api), &attempted, &failure);
        failures += Expect(
            endpoint == nullptr &&
                failure.stage == AudioFailureStage::GetAlignedBufferSize &&
                failure.result == AUDCLNT_E_BUFFER_SIZE_ERROR &&
                *shutdown_calls == 1,
            "x86 aligned size overflow rejects with owner cleanup");
    }
    return failures;
}

int TestStartAndRuntimeFailures() {
    int failures = 0;
    {
        auto api = std::make_unique<FakeWasapiApi>();
        auto* observed = api.get();
        EndpointInitialization attempted{};
        AudioFailure failure{};
        auto endpoint = WasapiEndpoint::Create(
            std::move(api), &attempted, &failure);
        failures += Expect(endpoint != nullptr, "endpoint for start failure");
        if (endpoint != nullptr) {
            observed->fail_call = Call::Start;
            observed->fail_occurrence = 1;
            observed->fail_result = E_ABORT;
            failures += Expect(
                endpoint->Start(&failure) == E_ABORT &&
                    failure.stage == AudioFailureStage::StartEndpoint &&
                    failure.result == E_ABORT,
                "start failure stage and HRESULT");
        }
    }
    {
        auto api = std::make_unique<FakeWasapiApi>();
        auto* observed = api.get();
        EndpointInitialization attempted{};
        AudioFailure failure{};
        auto endpoint = WasapiEndpoint::Create(
            std::move(api), &attempted, &failure);
        failures += Expect(endpoint != nullptr, "endpoint for runtime failures");
        if (endpoint == nullptr) {
            return failures + 1;
        }

        observed->wait_result = E_HANDLE;
        failures += Expect(
            endpoint->WaitForRender(10, &failure) == E_HANDLE &&
                failure.stage == AudioFailureStage::WaitRenderEvent &&
                failure.result == E_HANDLE,
            "wait failure stage and HRESULT");

        std::vector<std::int16_t> pcm(
            static_cast<std::size_t>(observed->actual_frames) * 2);
        observed->fail_call = Call::GetRenderBuffer;
        observed->fail_occurrence = 2;
        observed->matching_fail_call_count = 1;
        observed->fail_result = E_OUTOFMEMORY;
        failures += Expect(
            endpoint->SubmitPcm16(pcm, &failure) == E_OUTOFMEMORY &&
                failure.stage == AudioFailureStage::GetRenderBuffer,
            "runtime buffer acquisition failure stage");

        observed->fail_call = Call::ReleaseRenderBuffer;
        observed->fail_occurrence = 2;
        observed->matching_fail_call_count = 1;
        observed->fail_result = E_FAIL;
        failures += Expect(
            endpoint->SubmitPcm16(pcm, &failure) == E_FAIL &&
                failure.stage == AudioFailureStage::ReleaseRenderBuffer,
            "runtime buffer release failure stage");

        observed->fail_occurrence = 0;
        observed->clock_result = E_PENDING;
        EndpointClockPosition position{};
        failures += Expect(
            endpoint->ReadClock(&position, &failure) == E_PENDING &&
                failure.stage == AudioFailureStage::GetClockPosition &&
                failure.result == E_PENDING,
            "clock failure stage and HRESULT");
    }
    return failures;
}

int TestOwnerThreadShutdownIsIdempotent() {
    auto shutdown_calls = std::make_shared<std::uint32_t>();
    auto wrong_thread_calls = std::make_shared<std::uint32_t>();
    auto api = std::make_unique<FakeWasapiApi>();
    api->shutdown_call_count = shutdown_calls;
    api->wrong_thread_shutdown_call_count = wrong_thread_calls;
    EndpointInitialization attempted{};
    AudioFailure failure{};
    auto endpoint = WasapiEndpoint::Create(
        std::move(api),
        &attempted,
        &failure);

    int failures = Expect(endpoint != nullptr, "endpoint for explicit shutdown");
    if (endpoint == nullptr) {
        return failures + 1;
    }
    HRESULT wrong_thread_result{S_OK};
    std::thread wrong_thread([&] {
        wrong_thread_result = endpoint->ShutdownOnInitializingThread();
    });
    wrong_thread.join();
    failures += Expect(
        wrong_thread_result == RPC_E_WRONG_THREAD &&
            *shutdown_calls == 1 && *wrong_thread_calls == 1,
        "off-thread shutdown is rejected and remains retryable");
    failures += Expect(
        endpoint->ShutdownOnInitializingThread() == S_OK &&
            endpoint->ShutdownOnInitializingThread() == S_OK &&
            *shutdown_calls == 2,
        "owner-thread shutdown delegates once after wrong-thread rejection");
    endpoint.reset();
    failures += Expect(
        *shutdown_calls == 2,
        "destruction after explicit shutdown does not delegate again");

    auto fallback_calls = std::make_shared<std::uint32_t>();
    auto fallback_api = std::make_unique<FakeWasapiApi>();
    fallback_api->shutdown_call_count = fallback_calls;
    endpoint = WasapiEndpoint::Create(
        std::move(fallback_api),
        &attempted,
        &failure);
    failures += Expect(
        endpoint != nullptr,
        "endpoint for same-thread destructor fallback");
    endpoint.reset();
    failures += Expect(
        *fallback_calls == 1,
        "same-thread destructor fallback delegates shutdown once");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestDirectSuccessAndRuntimeForwarding();
    failures += TestAlignmentRetryUsesAuthoritativeFrames();
    failures += TestInitializationRejectionsAndStages();
    failures += TestSuccessfulZeroOutputsAreRejected();
    failures += TestUnaddressableOutputSizesAreRejected();
    failures += TestStartAndRuntimeFailures();
    failures += TestOwnerThreadShutdownIsIdempotent();
    return failures == 0 ? 0 : 1;
}
