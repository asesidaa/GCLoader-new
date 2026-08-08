// SPDX-License-Identifier: CC0-1.0

#include "Audio/AudioBackendController.h"

#include <dsound.h>

#include <utility>

namespace gc::audio {

AudioBackendController::AudioBackendController(
    AudioBackendControllerConfig config,
    IWasapiOutputBackendFactory& wasapi_factory,
    IAsioOutputBackendFactory& asio_factory,
    IAudioBackendControllerReporter& reporter) noexcept
    : config_(std::move(config)),
      wasapi_factory_(wasapi_factory),
      asio_factory_(asio_factory),
      reporter_(reporter)
{
}

HRESULT AudioBackendController::StartForWindow(HWND game_window) noexcept
{
    if (game_window == nullptr)
    {
        return DSERR_INVALIDPARAM;
    }

    {
        std::unique_lock lock(mutex_);
        while (state_ == State::starting)
        {
            condition_.wait(lock);
        }
        if (state_ == State::active_wasapi || state_ == State::active_asio)
        {
            return DS_OK;
        }
        if (state_ == State::failed)
        {
            return DSERR_NODRIVER;
        }
        state_ = State::starting;
    }

    std::optional<AsioFailure> asio_failure;
    std::unique_ptr<IAudioEngineServices> engine;
    if (config_.requested_backend == gc::config::AudioBackend::asio)
    {
        AsioFailure failure{};
        engine = asio_factory_.Start(
            game_window,
            config_.asio_request,
            &failure);
        if (engine != nullptr)
        {
            PublishResult(std::move(engine), State::active_asio);
            return DS_OK;
        }
        asio_failure = std::move(failure);
        reporter_.AsioFallback(*asio_failure);
    }

    AudioStartupFailure wasapi_failure{};
    if (config_.requested_backend == gc::config::AudioBackend::asio ||
        config_.requested_backend ==
            gc::config::AudioBackend::wasapi_exclusive)
    {
        engine = wasapi_factory_.Start(
            config_.wasapi_configured_duration,
            &wasapi_failure);
    }
    if (engine != nullptr)
    {
        PublishResult(std::move(engine), State::active_wasapi);
        return DS_OK;
    }

    reporter_.FatalStartupFailure(AudioBackendStartupFailure{
        .requested_backend = config_.requested_backend,
        .asio_failure = std::move(asio_failure),
        .wasapi_failure = std::move(wasapi_failure),
    });
    PublishResult(nullptr, State::failed);
    return DSERR_NODRIVER;
}

std::unique_ptr<MixerVoice> AudioBackendController::CreateVoice(
    const NormalizedSourceFormat& format,
    std::shared_ptr<AudioSnapshot> snapshot,
    std::shared_ptr<AudioCursorTimeline> timeline,
    VoiceUsage usage,
    ma_result* result) noexcept
{
    auto* engine = ActiveServices();
    if (engine == nullptr)
    {
        if (result != nullptr)
        {
            *result = MA_INVALID_OPERATION;
        }
        return nullptr;
    }
    return engine->CreateVoice(
        format,
        std::move(snapshot),
        std::move(timeline),
        usage,
        result);
}

std::optional<std::uint64_t>
AudioBackendController::CurrentOutputFrame() noexcept
{
    auto* engine = ActiveServices();
    return engine != nullptr ? engine->CurrentOutputFrame() : std::nullopt;
}

std::uint32_t AudioBackendController::endpoint_buffer_frames() const noexcept
{
    auto* engine = ActiveServices();
    return engine != nullptr ? engine->endpoint_buffer_frames() : 0;
}

std::uint32_t AudioBackendController::output_sample_rate() const noexcept
{
    auto* engine = ActiveServices();
    return engine != nullptr ? engine->output_sample_rate() : 0;
}

void AudioBackendController::CountPendingCursorQuery() noexcept
{
    if (auto* engine = ActiveServices(); engine != nullptr)
    {
        engine->CountPendingCursorQuery();
    }
}

void AudioBackendController::CountUnmappedCursorFailure() noexcept
{
    if (auto* engine = ActiveServices(); engine != nullptr)
    {
        engine->CountUnmappedCursorFailure();
    }
}

ActiveAudioBackend AudioBackendController::active_backend() const noexcept
{
    std::lock_guard lock(mutex_);
    switch (state_)
    {
    case State::active_wasapi:
        return ActiveAudioBackend::wasapi_exclusive;
    case State::active_asio:
        return ActiveAudioBackend::asio;
    case State::failed:
        return ActiveAudioBackend::failed;
    case State::not_started:
    case State::starting:
        return ActiveAudioBackend::none;
    }
    return ActiveAudioBackend::none;
}

gc::config::AudioBackend AudioBackendController::requested_backend() const noexcept
{
    return config_.requested_backend;
}

IAudioEngineServices* AudioBackendController::ActiveServices() const noexcept
{
    std::lock_guard lock(mutex_);
    return state_ == State::active_wasapi || state_ == State::active_asio
        ? engine_.get()
        : nullptr;
}

void AudioBackendController::PublishResult(
    std::unique_ptr<IAudioEngineServices> engine,
    State state) noexcept
{
    {
        std::lock_guard lock(mutex_);
        engine_ = std::move(engine);
        state_ = state;
    }
    condition_.notify_all();
}

} // namespace gc::audio
