#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioTypes.h"
#include "Audio/AudioSettings.h"
#include "Audio/DirectSound/DirectSoundFacade.h"
#include "Audio/Wasapi/WasapiEndpoint.h"

#include <Windows.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace gc::audio
{
    class IAudioEngineController : public IAudioEngineServices
    {
    public:
        virtual HRESULT StartForWindow(HWND game_window) noexcept = 0;
    };

    class IAudioBackendControllerFactory
    {
    public:
        virtual ~IAudioBackendControllerFactory() = default;
        virtual IAudioEngineController* GetOrCreate() noexcept = 0;
    };

    class IWasapiOutputBackendFactory
    {
    public:
        virtual ~IWasapiOutputBackendFactory() = default;
        virtual std::unique_ptr<IAudioEngineServices> Start(
            REFERENCE_TIME configured_duration,
            AudioStartupFailure*) noexcept = 0;
    };

    class IAsioOutputBackendFactory
    {
    public:
        virtual ~IAsioOutputBackendFactory() = default;
        virtual std::unique_ptr<IAudioEngineServices> Start(
            HWND game_window,
            const AsioStreamRequest&,
            AsioFailure*) noexcept = 0;
    };

    enum class ActiveAudioBackend : std::uint8_t
    {
        none,
        wasapi_exclusive,
        asio,
        failed,
    };

    struct AudioBackendControllerConfig
    {
        AudioBackend requested_backend{AudioBackend::directsound};
        REFERENCE_TIME wasapi_configured_duration{};
        AsioStreamRequest asio_request;
    };

    struct AudioBackendStartupFailure
    {
        AudioBackend requested_backend{AudioBackend::directsound};
        std::optional<AsioFailure> asio_failure;
        AudioStartupFailure wasapi_failure;
    };

    class IAudioBackendControllerReporter
    {
    public:
        virtual ~IAudioBackendControllerReporter() = default;
        virtual void FatalStartupFailure(
            const AudioBackendStartupFailure&) noexcept = 0;
        virtual void FatalControllerAllocationFailure() noexcept = 0;
    };

    class AudioBackendController final : public IAudioEngineController
    {
    public:
        AudioBackendController(
            AudioBackendControllerConfig,
            IWasapiOutputBackendFactory&,
            IAsioOutputBackendFactory&,
            IAudioBackendControllerReporter&) noexcept;

        HRESULT StartForWindow(HWND game_window) noexcept override;

        std::unique_ptr<MixerVoice> CreateVoice(
            const NormalizedSourceFormat&,
            std::shared_ptr<AudioSnapshot>,
            std::shared_ptr<AudioCursorTimeline>,
            VoiceUsage,
            ma_result*) noexcept override;
        std::optional<std::uint64_t> CurrentOutputFrame() noexcept override;
        std::uint32_t endpoint_buffer_frames() const noexcept override;
        std::uint32_t output_sample_rate() const noexcept override;
        void CountPendingCursorQuery() noexcept override;
        void CountUnmappedCursorFailure() noexcept override;

        [[nodiscard]] ActiveAudioBackend active_backend() const noexcept;
        [[nodiscard]] AudioBackend requested_backend() const noexcept;

    private:
        enum class State : std::uint8_t
        {
            not_started,
            starting,
            active_wasapi,
            active_asio,
            failed,
        };

        [[nodiscard]] IAudioEngineServices* ActiveServices() const noexcept;
        void PublishResult(
            std::unique_ptr<IAudioEngineServices>,
            State) noexcept;

        AudioBackendControllerConfig config_;
        IWasapiOutputBackendFactory& wasapi_factory_;
        IAsioOutputBackendFactory& asio_factory_;
        IAudioBackendControllerReporter& reporter_;
        mutable std::mutex mutex_;
        std::condition_variable condition_;
        State state_{State::not_started};
        std::unique_ptr<IAudioEngineServices> engine_;
    };
} // namespace gc::audio
