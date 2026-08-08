#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioTypes.h"
#include "Audio/DirectSound/DirectSoundFacade.h"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace gc::audio {

struct AsioRuntimeCountersSnapshot
{
    std::uint64_t callbacks{};
    std::uint64_t time_info_callbacks{};
    std::uint64_t legacy_callbacks{};
    std::uint64_t deferred_callbacks{};
    std::uint64_t deadline_misses{};
    std::uint64_t silence_substitutions{};
    std::uint64_t overload_messages{};
    std::uint64_t reset_requests{};
    std::uint64_t resync_requests{};
    std::uint64_t latency_change_requests{};
    std::uint64_t buffer_size_change_requests{};
    std::uint64_t sample_rate_change_requests{};
    std::uint64_t sample_position_discontinuities{};
    std::uint64_t render_gap_frames{};
    std::uint64_t maximum_callback_ticks{};
    std::uint64_t maximum_render_ticks{};
    std::uint64_t qpc_frequency{};
    std::uint64_t pending_cursor_queries{};
    std::uint64_t unmapped_cursor_failures{};
    MixerDiagnosticsSnapshot mixer{};
};

class IAsioOutputObserver
{
public:
    virtual ~IAsioOutputObserver() = default;
    virtual void StartupSucceeded(
        const AsioCapabilityReport&) noexcept = 0;
    virtual void RuntimeSummary(
        const AsioRuntimeCountersSnapshot&) noexcept = 0;
    virtual void RuntimeFailed(
        const AsioFailure&,
        const AsioRuntimeCountersSnapshot&) noexcept = 0;
};

class AsioOutputBackend;

namespace detail {
class AsioOutputBackendState;
struct AsioOutputBackendActions;
[[nodiscard]] std::unique_ptr<AsioOutputBackend>
StartAsioOutputBackendAndWait(
    HWND,
    const AsioStreamRequest&,
    std::unique_ptr<IAsioRegistrySource>,
    std::unique_ptr<IAsioDriverFactory>,
    std::shared_ptr<IAsioOutputObserver>,
    std::shared_ptr<const ma_allocation_callbacks>,
    DWORD,
    const AsioOutputBackendActions&,
    AsioFailure*) noexcept;
} // namespace detail

class AsioOutputBackend final : public IAudioEngineServices
{
public:
    static std::unique_ptr<AsioOutputBackend> StartAndWait(
        HWND game_window,
        const AsioStreamRequest&,
        std::unique_ptr<IAsioRegistrySource>,
        std::unique_ptr<IAsioDriverFactory>,
        std::shared_ptr<IAsioOutputObserver>,
        std::shared_ptr<const ma_allocation_callbacks>,
        DWORD startup_clock_timeout_ms,
        AsioFailure*) noexcept;
    ~AsioOutputBackend();

    AsioOutputBackend(const AsioOutputBackend&) = delete;
    AsioOutputBackend& operator=(const AsioOutputBackend&) = delete;

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>,
        VoiceUsage,
        ma_result*) noexcept override;
    std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
    std::uint32_t endpoint_buffer_frames() const noexcept override;
    std::uint32_t output_sample_rate() const noexcept override;
    void CountPendingCursorQuery() noexcept override;
    void CountUnmappedCursorFailure() noexcept override;

private:
    friend std::unique_ptr<AsioOutputBackend>
    detail::StartAsioOutputBackendAndWait(
        HWND,
        const AsioStreamRequest&,
        std::unique_ptr<IAsioRegistrySource>,
        std::unique_ptr<IAsioDriverFactory>,
        std::shared_ptr<IAsioOutputObserver>,
        std::shared_ptr<const ma_allocation_callbacks>,
        DWORD,
        const detail::AsioOutputBackendActions&,
        AsioFailure*) noexcept;

    explicit AsioOutputBackend(
        std::unique_ptr<detail::AsioOutputBackendState>) noexcept;

    std::unique_ptr<detail::AsioOutputBackendState> state_;
};

} // namespace gc::audio
