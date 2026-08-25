#pragma once

#include "Audio/Wasapi/WasapiAudioTypes.h"

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace gc::audio {

enum class AudioFailureStage : std::uint32_t {
    None,
    InitializationTimeout,
    InitializeMixer,
    CoInitialize,
    OpenDefaultEndpoint,
    ActivateAudioClient,
    IsFormatSupported,
    InvalidConfiguredDuration,
    GetDevicePeriod,
    ConfiguredDurationBelowMinimum,
    InitializeExclusive,
    GetAlignedBufferSize,
    ReactivateAudioClient,
    RetryInitializeExclusive,
    GetActualBufferSize,
    CreateRenderEvent,
    SetEventHandle,
    GetRenderService,
    GetClockService,
    GetClockFrequency,
    PrefillGetBuffer,
    PrefillReleaseBuffer,
    RegisterMmcss,
    SetMmcssPriority,
    StartEndpoint,
    WaitRenderEvent,
    GetRenderBuffer,
    ReleaseRenderBuffer,
    GetClockPosition,
    InvalidClockPosition,
    ChronicOutputGap,
};

struct AudioFailure {
    AudioFailureStage stage{AudioFailureStage::None};
    HRESULT result{S_OK};
};

inline constexpr std::size_t kEndpointFormatCandidateCount = 4;

struct EndpointFormatAttempt {
    EndpointPcmFormat format{};
    HRESULT result{E_NOTIMPL};
};

struct EndpointInitialization {
    std::wstring endpoint_name;
    std::wstring endpoint_id;
    REFERENCE_TIME default_period{};
    REFERENCE_TIME minimum_period{};
    REFERENCE_TIME configured_duration{};
    REFERENCE_TIME requested_duration{};
    REFERENCE_TIME stream_latency{};
    HRESULT stream_latency_result{E_NOTIMPL};
    bool stream_latency_available{};
    std::uint32_t actual_buffer_frames{};
    std::uint64_t clock_frequency{};
    bool alignment_retry{};
    std::array<EndpointFormatAttempt, kEndpointFormatCandidateCount>
        format_attempts{};
    std::uint8_t format_attempt_count{};
    EndpointPcmFormat selected_format{};
    bool has_selected_format{};
};

struct EndpointClockPosition {
    std::uint64_t position{};
    std::uint64_t qpc_100ns{};
};

struct AudioStartupFailure {
    AudioFailure failure{};
    EndpointInitialization attempted{};
};

class IWasapiApi {
public:
    virtual ~IWasapiApi() = default;
    virtual HRESULT InitializeComMta() noexcept = 0;
    virtual HRESULT OpenDefaultConsoleEndpoint(
        std::wstring*, std::wstring*) noexcept = 0;
    virtual HRESULT ActivateAudioClient() noexcept = 0;
    virtual HRESULT IsExactFormatSupported(
        const EndpointPcmFormat&) noexcept = 0;
    virtual HRESULT GetDevicePeriod(
        REFERENCE_TIME*, REFERENCE_TIME*) noexcept = 0;
    virtual HRESULT InitializeExclusiveEvent(
        REFERENCE_TIME, REFERENCE_TIME,
        const EndpointPcmFormat&) noexcept = 0;
    virtual HRESULT GetBufferSize(std::uint32_t*) noexcept = 0;
    virtual HRESULT GetStreamLatency(REFERENCE_TIME*) noexcept = 0;
    virtual void ReleaseAudioClient() noexcept = 0;
    virtual HRESULT CreateRenderEvent() noexcept = 0;
    virtual HRESULT SetEventHandle() noexcept = 0;
    virtual HRESULT GetRenderService() noexcept = 0;
    virtual HRESULT GetClockService() noexcept = 0;
    virtual HRESULT GetClockFrequency(std::uint64_t*) noexcept = 0;
    virtual HRESULT GetRenderBuffer(std::uint32_t, BYTE**) noexcept = 0;
    virtual HRESULT ReleaseRenderBuffer(std::uint32_t, DWORD) noexcept = 0;
    virtual HRESULT RegisterMmcssProAudio() noexcept = 0;
    virtual HRESULT SetMmcssCriticalPriority() noexcept = 0;
    virtual HRESULT Start() noexcept = 0;
    virtual HRESULT WaitForRender(DWORD) noexcept = 0;
    virtual HRESULT GetClockPosition(
        std::uint64_t*, std::uint64_t*) noexcept = 0;
    virtual HRESULT ShutdownOnInitializingThread() noexcept = 0;
};

class WasapiEndpoint final {
public:
    static std::unique_ptr<WasapiEndpoint> Create(
        std::unique_ptr<IWasapiApi>,
        REFERENCE_TIME configured_duration,
        EndpointInitialization*,
        AudioFailure*);
    ~WasapiEndpoint();

    HRESULT Start(AudioFailure*) noexcept;
    HRESULT WaitForRender(DWORD, AudioFailure*) noexcept;
    HRESULT SubmitPcm16(
        std::span<const std::int16_t>, AudioFailure*) noexcept;
    HRESULT TrySubmitSilence() noexcept;
    HRESULT ReadClock(EndpointClockPosition*, AudioFailure*) noexcept;
    HRESULT ShutdownOnInitializingThread() noexcept;
    const EndpointInitialization& initialization() const noexcept;

private:
    WasapiEndpoint(
        std::unique_ptr<IWasapiApi>,
        REFERENCE_TIME configured_duration) noexcept;
    HRESULT Initialize(EndpointInitialization*, AudioFailure*);
    HRESULT Fail(
        AudioFailureStage, HRESULT,
        EndpointInitialization*, AudioFailure*) const;

    std::unique_ptr<IWasapiApi> api_;
    EndpointInitialization initialization_{};
    bool shutdown_complete_{};
};

std::unique_ptr<IWasapiApi> CreateProductionWasapiApi() noexcept;

} // namespace gc::audio
