#include "WasapiEndpoint.h"

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace gc::audio {
namespace {

WAVEFORMATEX OutputFormat() noexcept {
    return {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = kOutputChannels,
        .nSamplesPerSec = kOutputSampleRate,
        .nAvgBytesPerSec = kOutputAverageBytesPerSecond,
        .nBlockAlign = kOutputBlockAlign,
        .wBitsPerSample = kOutputBitsPerSample,
        .cbSize = 0,
    };
}

constexpr bool CanAddressOutputBuffer(std::uint32_t frames) noexcept {
    return frames <=
        std::numeric_limits<std::size_t>::max() / kOutputChannels;
}

HRESULT LastErrorAsHresult() noexcept {
    const auto error = GetLastError();
    return HRESULT_FROM_WIN32(
        error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
}

class Win32WasapiApi final : public IWasapiApi {
public:
    ~Win32WasapiApi() override {
        if (shutdown_complete_) {
            return;
        }
        if (IsInitializingThread()) {
            ShutdownOnInitializingThread();
            return;
        }

        mmcss_handle_ = nullptr;
        DetachComObjectsForProcessCleanup();
    }

    HRESULT ShutdownOnInitializingThread() noexcept override {
        if (shutdown_complete_) {
            return S_OK;
        }
        if (!IsInitializingThread()) {
            return RPC_E_WRONG_THREAD;
        }

        if (mmcss_handle_ != nullptr) {
            AvRevertMmThreadCharacteristics(mmcss_handle_);
            mmcss_handle_ = nullptr;
        }
        if (render_event_ != nullptr) {
            CloseHandle(render_event_);
            render_event_ = nullptr;
        }
        clock_.Reset();
        render_client_.Reset();
        client_.Reset();
        device_.Reset();
        enumerator_.Reset();
        CoUninitialize();
        com_initialized_ = false;
        shutdown_complete_ = true;
        return S_OK;
    }

    HRESULT InitializeComMta() noexcept override {
        const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(result)) {
            com_initialized_ = true;
            initializing_thread_id_ = GetCurrentThreadId();
        }
        return result;
    }

    HRESULT OpenDefaultConsoleEndpoint(
        std::wstring* endpoint_name,
        std::wstring* endpoint_id) noexcept override {
        if (endpoint_name == nullptr || endpoint_id == nullptr) {
            return E_POINTER;
        }

        auto result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(enumerator_.ReleaseAndGetAddressOf()));
        if (FAILED(result)) {
            return result;
        }
        result = enumerator_->GetDefaultAudioEndpoint(
            eRender,
            eConsole,
            device_.ReleaseAndGetAddressOf());
        if (FAILED(result)) {
            return result;
        }

        LPWSTR raw_id{};
        result = device_->GetId(&raw_id);
        if (FAILED(result)) {
            return result;
        }

        Microsoft::WRL::ComPtr<IPropertyStore> properties;
        result = device_->OpenPropertyStore(
            STGM_READ,
            properties.ReleaseAndGetAddressOf());
        if (FAILED(result)) {
            CoTaskMemFree(raw_id);
            return result;
        }

        PROPVARIANT raw_name{};
        PropVariantInit(&raw_name);
        result = properties->GetValue(PKEY_Device_FriendlyName, &raw_name);
        if (FAILED(result)) {
            PropVariantClear(&raw_name);
            CoTaskMemFree(raw_id);
            return result;
        }
        if (raw_id == nullptr || raw_name.vt != VT_LPWSTR ||
            raw_name.pwszVal == nullptr) {
            PropVariantClear(&raw_name);
            CoTaskMemFree(raw_id);
            return E_UNEXPECTED;
        }

        try {
            std::wstring selected_name{raw_name.pwszVal};
            std::wstring selected_id{raw_id};
            *endpoint_name = std::move(selected_name);
            *endpoint_id = std::move(selected_id);
        } catch (const std::bad_alloc&) {
            result = E_OUTOFMEMORY;
        }
        PropVariantClear(&raw_name);
        CoTaskMemFree(raw_id);
        return result;
    }

    HRESULT ActivateAudioClient() noexcept override {
        if (device_ == nullptr) {
            return E_UNEXPECTED;
        }
        return device_->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(client_.ReleaseAndGetAddressOf()));
    }

    HRESULT IsExactFormatSupported(
        const WAVEFORMATEX& format) noexcept override {
        return client_ == nullptr
            ? E_UNEXPECTED
            : client_->IsFormatSupported(
                  AUDCLNT_SHAREMODE_EXCLUSIVE,
                  &format,
                  nullptr);
    }

    HRESULT GetDevicePeriod(
        REFERENCE_TIME* default_period,
        REFERENCE_TIME* minimum_period) noexcept override {
        return client_ == nullptr
            ? E_UNEXPECTED
            : client_->GetDevicePeriod(default_period, minimum_period);
    }

    HRESULT InitializeExclusiveEvent(
        REFERENCE_TIME buffer_duration,
        REFERENCE_TIME periodicity,
        const WAVEFORMATEX& format) noexcept override {
        return client_ == nullptr
            ? E_UNEXPECTED
            : client_->Initialize(
                  AUDCLNT_SHAREMODE_EXCLUSIVE,
                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                  buffer_duration,
                  periodicity,
                  &format,
                  nullptr);
    }

    HRESULT GetBufferSize(std::uint32_t* frames) noexcept override {
        return client_ == nullptr
            ? E_UNEXPECTED
            : client_->GetBufferSize(frames);
    }

    void ReleaseAudioClient() noexcept override {
        if (!IsInitializingThread()) {
            return;
        }
        clock_.Reset();
        render_client_.Reset();
        client_.Reset();
    }

    HRESULT CreateRenderEvent() noexcept override {
        if (render_event_ != nullptr) {
            return E_UNEXPECTED;
        }
        render_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return render_event_ == nullptr ? LastErrorAsHresult() : S_OK;
    }

    HRESULT SetEventHandle() noexcept override {
        return client_ == nullptr || render_event_ == nullptr
            ? E_UNEXPECTED
            : client_->SetEventHandle(render_event_);
    }

    HRESULT GetRenderService() noexcept override {
        return client_ == nullptr
            ? E_UNEXPECTED
            : client_->GetService(
                  IID_PPV_ARGS(render_client_.ReleaseAndGetAddressOf()));
    }

    HRESULT GetClockService() noexcept override {
        return client_ == nullptr
            ? E_UNEXPECTED
            : client_->GetService(
                  IID_PPV_ARGS(clock_.ReleaseAndGetAddressOf()));
    }

    HRESULT GetClockFrequency(std::uint64_t* frequency) noexcept override {
        return clock_ == nullptr
            ? E_UNEXPECTED
            : clock_->GetFrequency(frequency);
    }

    HRESULT GetRenderBuffer(
        std::uint32_t frames,
        BYTE** buffer) noexcept override {
        return render_client_ == nullptr
            ? E_UNEXPECTED
            : render_client_->GetBuffer(frames, buffer);
    }

    HRESULT ReleaseRenderBuffer(
        std::uint32_t frames,
        DWORD flags) noexcept override {
        return render_client_ == nullptr
            ? E_UNEXPECTED
            : render_client_->ReleaseBuffer(frames, flags);
    }

    HRESULT RegisterMmcssProAudio() noexcept override {
        if (!com_initialized_ ||
            GetCurrentThreadId() != initializing_thread_id_) {
            return RPC_E_WRONG_THREAD;
        }
        DWORD task_index{};
        mmcss_handle_ = AvSetMmThreadCharacteristicsW(
            L"Pro Audio",
            &task_index);
        return mmcss_handle_ == nullptr ? LastErrorAsHresult() : S_OK;
    }

    HRESULT SetMmcssCriticalPriority() noexcept override {
        if (mmcss_handle_ == nullptr) {
            return E_UNEXPECTED;
        }
        return AvSetMmThreadPriority(
                   mmcss_handle_,
                   AVRT_PRIORITY_CRITICAL)
            ? S_OK
            : LastErrorAsHresult();
    }

    HRESULT Start() noexcept override {
        return client_ == nullptr ? E_UNEXPECTED : client_->Start();
    }

    HRESULT WaitForRender(DWORD timeout_ms) noexcept override {
        if (render_event_ == nullptr) {
            return E_UNEXPECTED;
        }
        const auto result = WaitForSingleObject(render_event_, timeout_ms);
        if (result == WAIT_OBJECT_0) {
            return S_OK;
        }
        if (result == WAIT_TIMEOUT) {
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        return result == WAIT_FAILED ? LastErrorAsHresult() : E_UNEXPECTED;
    }

    HRESULT GetClockPosition(
        std::uint64_t* position,
        std::uint64_t* qpc_100ns) noexcept override {
        return clock_ == nullptr
            ? E_UNEXPECTED
            : clock_->GetPosition(position, qpc_100ns);
    }

private:
    bool IsInitializingThread() const noexcept {
        return com_initialized_ &&
            GetCurrentThreadId() == initializing_thread_id_;
    }

    void DetachComObjectsForProcessCleanup() noexcept {
        static_cast<void>(clock_.Detach());
        static_cast<void>(render_client_.Detach());
        static_cast<void>(client_.Detach());
        static_cast<void>(device_.Detach());
        static_cast<void>(enumerator_.Detach());
    }

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IMMDevice> device_;
    Microsoft::WRL::ComPtr<IAudioClient> client_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> render_client_;
    Microsoft::WRL::ComPtr<IAudioClock> clock_;
    HANDLE render_event_{};
    HANDLE mmcss_handle_{};
    DWORD initializing_thread_id_{};
    bool com_initialized_{};
    bool shutdown_complete_{};
};

} // namespace

WasapiEndpoint::WasapiEndpoint(
    std::unique_ptr<IWasapiApi> api,
    REFERENCE_TIME configured_duration) noexcept
    : api_(std::move(api)) {
    initialization_.configured_duration = configured_duration;
}

WasapiEndpoint::~WasapiEndpoint() {
    static_cast<void>(ShutdownOnInitializingThread());
}

std::unique_ptr<WasapiEndpoint> WasapiEndpoint::Create(
    std::unique_ptr<IWasapiApi> api,
    REFERENCE_TIME configured_duration,
    EndpointInitialization* attempted,
    AudioFailure* failure) {
    if (attempted != nullptr) {
        *attempted = {};
    }
    if (failure != nullptr) {
        *failure = {};
    }
    if (api == nullptr) {
        if (failure != nullptr) {
            *failure = {AudioFailureStage::CoInitialize, E_INVALIDARG};
        }
        return nullptr;
    }

    auto endpoint = std::unique_ptr<WasapiEndpoint>(
        new (std::nothrow) WasapiEndpoint(
            std::move(api),
            configured_duration));
    if (endpoint == nullptr) {
        if (failure != nullptr) {
            *failure = {AudioFailureStage::CoInitialize, E_OUTOFMEMORY};
        }
        return nullptr;
    }
    if (endpoint->Initialize(attempted, failure) != S_OK) {
        return nullptr;
    }
    return endpoint;
}

HRESULT WasapiEndpoint::Fail(
    AudioFailureStage stage,
    HRESULT result,
    EndpointInitialization* attempted,
    AudioFailure* failure) {
    if (attempted != nullptr) {
        *attempted = initialization_;
    }
    if (failure != nullptr) {
        *failure = {stage, result};
    }
    return result;
}

HRESULT WasapiEndpoint::Initialize(
    EndpointInitialization* attempted,
    AudioFailure* failure) {
    auto result = api_->InitializeComMta();
    if (FAILED(result)) {
        return Fail(AudioFailureStage::CoInitialize, result, attempted, failure);
    }

    result = api_->OpenDefaultConsoleEndpoint(
        &initialization_.endpoint_name,
        &initialization_.endpoint_id);
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::OpenDefaultEndpoint,
            result,
            attempted,
            failure);
    }
    if (attempted != nullptr) {
        *attempted = initialization_;
    }

    result = api_->ActivateAudioClient();
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::ActivateAudioClient,
            result,
            attempted,
            failure);
    }

    const auto output_format = OutputFormat();
    result = api_->IsExactFormatSupported(output_format);
    if (result != S_OK) {
        return Fail(
            AudioFailureStage::IsFormatSupported,
            result,
            attempted,
            failure);
    }

    result = api_->GetDevicePeriod(
        &initialization_.default_period,
        &initialization_.minimum_period);
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::GetDevicePeriod,
            result,
            attempted,
            failure);
    }
    if (attempted != nullptr) {
        *attempted = initialization_;
    }

    auto requested = std::max(
        initialization_.minimum_period,
        initialization_.configured_duration);
    initialization_.requested_duration = requested;
    result = api_->InitializeExclusiveEvent(
        requested,
        requested,
        output_format);
    std::uint32_t aligned_frames{};
    if (result == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        initialization_.alignment_retry = true;
        if (attempted != nullptr) {
            *attempted = initialization_;
        }
        result = api_->GetBufferSize(&aligned_frames);
        if (FAILED(result)) {
            return Fail(
                AudioFailureStage::GetAlignedBufferSize,
                result,
                attempted,
                failure);
        }
        if (aligned_frames == 0 ||
            !CanAddressOutputBuffer(aligned_frames)) {
            return Fail(
                AudioFailureStage::GetAlignedBufferSize,
                AUDCLNT_E_BUFFER_SIZE_ERROR,
                attempted,
                failure);
        }
        api_->ReleaseAudioClient();
        result = api_->ActivateAudioClient();
        if (FAILED(result)) {
            return Fail(
                AudioFailureStage::ReactivateAudioClient,
                result,
                attempted,
                failure);
        }
        requested = FramesToReferenceTime(
            aligned_frames,
            kOutputSampleRate);
        initialization_.requested_duration = requested;
        if (attempted != nullptr) {
            *attempted = initialization_;
        }
        result = api_->InitializeExclusiveEvent(
            requested,
            requested,
            output_format);
        if (FAILED(result)) {
            return Fail(
                AudioFailureStage::RetryInitializeExclusive,
                result,
                attempted,
                failure);
        }
    } else if (FAILED(result)) {
        return Fail(
            AudioFailureStage::InitializeExclusive,
            result,
            attempted,
            failure);
    }

    result = api_->GetBufferSize(&initialization_.actual_buffer_frames);
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::GetActualBufferSize,
            result,
            attempted,
            failure);
    }
    if (initialization_.actual_buffer_frames == 0 ||
        !CanAddressOutputBuffer(initialization_.actual_buffer_frames) ||
        (initialization_.alignment_retry &&
         initialization_.actual_buffer_frames != aligned_frames) ||
        (!initialization_.alignment_retry &&
         initialization_.actual_buffer_frames >
             ReferenceTimeToFramesCeil(
                 initialization_.requested_duration,
                 kOutputSampleRate))) {
        return Fail(
            AudioFailureStage::GetActualBufferSize,
            AUDCLNT_E_BUFFER_SIZE_ERROR,
            attempted,
            failure);
    }
    if (attempted != nullptr) {
        *attempted = initialization_;
    }

    result = api_->CreateRenderEvent();
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::CreateRenderEvent,
            result,
            attempted,
            failure);
    }
    result = api_->SetEventHandle();
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::SetEventHandle,
            result,
            attempted,
            failure);
    }
    result = api_->GetRenderService();
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::GetRenderService,
            result,
            attempted,
            failure);
    }
    result = api_->GetClockService();
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::GetClockService,
            result,
            attempted,
            failure);
    }
    result = api_->GetClockFrequency(&initialization_.clock_frequency);
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::GetClockFrequency,
            result,
            attempted,
            failure);
    }
    if (initialization_.clock_frequency == 0) {
        return Fail(
            AudioFailureStage::GetClockFrequency,
            E_UNEXPECTED,
            attempted,
            failure);
    }
    if (attempted != nullptr) {
        *attempted = initialization_;
    }

    BYTE* prefill{};
    result = api_->GetRenderBuffer(
        initialization_.actual_buffer_frames,
        &prefill);
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::PrefillGetBuffer,
            result,
            attempted,
            failure);
    }
    result = api_->ReleaseRenderBuffer(
        initialization_.actual_buffer_frames,
        AUDCLNT_BUFFERFLAGS_SILENT);
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::PrefillReleaseBuffer,
            result,
            attempted,
            failure);
    }
    result = api_->RegisterMmcssProAudio();
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::RegisterMmcss,
            result,
            attempted,
            failure);
    }
    result = api_->SetMmcssCriticalPriority();
    if (FAILED(result)) {
        return Fail(
            AudioFailureStage::SetMmcssPriority,
            result,
            attempted,
            failure);
    }

    if (attempted != nullptr) {
        *attempted = initialization_;
    }
    return S_OK;
}

HRESULT WasapiEndpoint::Start(AudioFailure* failure) noexcept {
    const auto result = api_->Start();
    if (FAILED(result) && failure != nullptr) {
        *failure = {AudioFailureStage::StartEndpoint, result};
    }
    return result;
}

HRESULT WasapiEndpoint::WaitForRender(
    DWORD timeout_ms,
    AudioFailure* failure) noexcept {
    const auto result = api_->WaitForRender(timeout_ms);
    if (FAILED(result) && failure != nullptr) {
        *failure = {AudioFailureStage::WaitRenderEvent, result};
    }
    return result;
}

HRESULT WasapiEndpoint::SubmitPcm16(
    std::span<const std::int16_t> samples,
    AudioFailure* failure) noexcept {
    const auto required_samples =
        static_cast<std::size_t>(initialization_.actual_buffer_frames) *
        kOutputChannels;
    if (samples.size() != required_samples) {
        return E_INVALIDARG;
    }

    BYTE* destination{};
    auto result = api_->GetRenderBuffer(
        initialization_.actual_buffer_frames,
        &destination);
    if (FAILED(result)) {
        if (failure != nullptr) {
            *failure = {AudioFailureStage::GetRenderBuffer, result};
        }
        return result;
    }
    std::memcpy(
        destination,
        samples.data(),
        samples.size_bytes());
    result = api_->ReleaseRenderBuffer(
        initialization_.actual_buffer_frames,
        0);
    if (FAILED(result) && failure != nullptr) {
        *failure = {AudioFailureStage::ReleaseRenderBuffer, result};
    }
    return result;
}

HRESULT WasapiEndpoint::TrySubmitSilence() noexcept {
    BYTE* destination{};
    auto result = api_->GetRenderBuffer(
        initialization_.actual_buffer_frames,
        &destination);
    if (FAILED(result)) {
        return result;
    }
    return api_->ReleaseRenderBuffer(
        initialization_.actual_buffer_frames,
        AUDCLNT_BUFFERFLAGS_SILENT);
}

HRESULT WasapiEndpoint::ReadClock(
    EndpointClockPosition* position,
    AudioFailure* failure) noexcept {
    if (position == nullptr) {
        return E_INVALIDARG;
    }
    const auto result = api_->GetClockPosition(
        &position->position,
        &position->qpc_100ns);
    if (FAILED(result) && failure != nullptr) {
        *failure = {AudioFailureStage::GetClockPosition, result};
    }
    return result;
}

HRESULT WasapiEndpoint::ShutdownOnInitializingThread() noexcept {
    if (shutdown_complete_) {
        return S_OK;
    }
    const auto result = api_->ShutdownOnInitializingThread();
    if (SUCCEEDED(result)) {
        shutdown_complete_ = true;
    }
    return result;
}

const EndpointInitialization& WasapiEndpoint::initialization() const noexcept {
    return initialization_;
}

std::unique_ptr<IWasapiApi> CreateProductionWasapiApi() noexcept {
    return std::unique_ptr<IWasapiApi>(new (std::nothrow) Win32WasapiApi());
}

} // namespace gc::audio
