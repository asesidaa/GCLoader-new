#include "Audio/AudioBackendComposition.h"
#include "Audio/AudioBackendController.h"
#include "Audio/AudioDiagnostics.h"
#include "Audio/Asio/AsioOutputBackend.h"

#include <utility>

namespace gc::audio {
namespace {
constexpr REFERENCE_TIME BufferMillisecondsToReferenceTime(std::uint32_t value) noexcept
{
    return static_cast<REFERENCE_TIME>(value) * 10'000;
}
std::uint32_t configured_wasapi_buffer_ms(
    const AudioSettings& settings) noexcept
{
    if (const auto* wasapi = std::get_if<WasapiExclusiveSettings>(
        &settings.selection()))
    {
        return wasapi->buffer_ms();
    }
    return 0;
}

bool configured_wasapi_exact_history(
    const AudioSettings& settings) noexcept
{
    if (const auto* wasapi = std::get_if<WasapiExclusiveSettings>(
        &settings.selection()))
    {
        return wasapi->exact_history_required();
    }
    return false;
}


} // namespace

AudioBackendControllerConfig BuildAudioControllerConfig(
    const AudioSettings& settings)
{
    AudioBackendControllerConfig config{
        .requested_backend = settings.backend(),
        .wasapi_configured_duration = BufferMillisecondsToReferenceTime(
            configured_wasapi_buffer_ms(settings)),
    };
    if (const auto* asio = std::get_if<AsioSettings>(
        &settings.selection()))
    {
        config.asio_request = {
            .driver_name = asio->driver_name(),
            .buffer_frames = asio->buffer_frames(),
            .output_base_channel = asio->output_base_channel(),
        };
    }
    return config;
}


AudioBackendComposition::AudioBackendComposition(const AudioSettings& settings)
    : backend_(settings.backend()),
      exact_history_(configured_wasapi_exact_history(settings)),
      registry_(std::make_unique<ProductionAsioRegistrySource>()),
      driver_factory_(std::make_unique<ProductionAsioDriverFactory>())
{
}

std::unique_ptr<IAudioEngineServices> AudioBackendComposition::StartWasapi(
    REFERENCE_TIME duration, AudioStartupFailure* startup_failure) noexcept
{
    std::shared_ptr<IAudioEngineObserver> observer;
    try { observer = MakeAudioObserver(backend_); }
    catch (...)
    {
        if (startup_failure)
        {
            *startup_failure = {};
            startup_failure->failure = {AudioFailureStage::InitializeMixer, E_OUTOFMEMORY};
        }
        return nullptr;
    }
    ReportAudioBufferHandoff("production_engine_start", duration);
    if (startup_failure) *startup_failure = {};
    auto api = CreateProductionWasapiApi();
    if (!api)
    {
        if (startup_failure)
            startup_failure->failure = {AudioFailureStage::InitializeMixer, E_OUTOFMEMORY};
        return nullptr;
    }
    return ExclusiveAudioEngine::StartAndWait(
        std::move(api), std::move(observer), 10'000, duration, exact_history_,
        std::shared_ptr<const ma_allocation_callbacks>{}, startup_failure);
}

std::unique_ptr<IAudioEngineServices> AudioBackendComposition::StartAsio(
    HWND window, const AsioStreamRequest& request) noexcept
{
    return AsioOutputBackend::Start(
        window, request, std::move(registry_), std::move(driver_factory_));
}
} // namespace gc::audio
