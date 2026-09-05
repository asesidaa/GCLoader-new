#include "Audio/AudioRuntimeState.h"
#include "Audio/AudioBackendComposition.h"
#include "Audio/AudioBackendController.h"
#include "Audio/AudioContractFatal.h"
#include "Audio/AudioDiagnostics.h"
#include "Audio/DirectSound/DirectSoundHook.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace gc::audio {
namespace {
struct AudioRuntimeState final {
    explicit AudioRuntimeState(AudioSettings startup_settings)
        : settings(std::move(startup_settings)),
          config(BuildAudioControllerConfig(settings)), backends(settings)
    {
        if (config.requested_backend == AudioBackend::asio)
            controller = std::make_unique<AudioBackendController>(config, backends);
        if (config.requested_backend == AudioBackend::wasapi_exclusive)
            ReportAudioBufferHandoff("detour_state", config.wasapi_configured_duration);
    }

    IAudioEngineController* GetOrCreateController() noexcept
    {
        if (config.requested_backend == AudioBackend::asio) return controller.get();
        std::lock_guard lock(mutex);
        if (attempted) return controller.get();
        attempted = true;
        try { controller = std::make_unique<AudioBackendController>(config, backends); }
        catch (...) { controller.reset(); }
        return controller.get();
    }

    const AudioSettings settings;
    const AudioBackendControllerConfig config;
    AudioBackendComposition backends;
    std::mutex mutex;
    bool attempted{};
    std::unique_ptr<AudioBackendController> controller;
};

std::atomic<AudioRuntimeState*> g_runtime{};
std::atomic_bool g_prepared{};
} // namespace

std::expected<void, AudioRuntimeError> PrepareAndPublishAudioRuntime(
    AudioSettings settings) noexcept
{
    if (g_prepared.exchange(true, std::memory_order_acq_rel))
        return std::unexpected(AudioRuntimeError{
            AudioRuntimeStage::already_prepared, ERROR_ALREADY_INITIALIZED});
    try
    {
        const auto backend = settings.backend();
        const auto* wasapi = std::get_if<WasapiExclusiveSettings>(&settings.selection());
        ReportAudioConfig(backend, wasapi ? wasapi->buffer_ms() : 0);
        if (backend == AudioBackend::directsound) return {};
        auto* state = new AudioRuntimeState(std::move(settings));
        g_runtime.store(state, std::memory_order_release);
        return {};
    }
    catch (...)
    {
        return std::unexpected(AudioRuntimeError{
            AudioRuntimeStage::construction, ERROR_NOT_ENOUGH_MEMORY});
    }
}

bool IsAsioRuntimePublished() noexcept
{
    const auto* state = g_runtime.load(std::memory_order_acquire);
    return state && state->settings.backend() == AudioBackend::asio;
}

IAudioEngineController* GetOrCreatePublishedAudioController() noexcept
{
    auto* state = g_runtime.load(std::memory_order_acquire);
    return state ? state->GetOrCreateController() : nullptr;
}

std::expected<void, hooking::HookError> AddPublishedAudioRuntimeHook(
    hooking::HookPlan& hooks) noexcept
{
    auto* state = g_runtime.load(std::memory_order_acquire);
    if (!g_prepared.load(std::memory_order_acquire))
        return std::unexpected(hooking::HookError{
            .stage = hooking::HookStage::invalid_plan,
            .identity = {"Audio", "runtime_not_prepared"}, .win32_error = ERROR_INVALID_STATE});
    if (!state) return {};
    return AddDirectSoundHook(hooks);
}

void ReleaseAudioRuntimeAtOrdinaryAsioClose(std::uintptr_t site_rva) noexcept
{
    auto* owner = g_runtime.exchange(nullptr, std::memory_order_acq_rel);
    if (!owner) FailAudioContract(AudioContractFatalReason::AsioOwnershipFailure, site_rva);
    delete owner;
}
} // namespace gc::audio
