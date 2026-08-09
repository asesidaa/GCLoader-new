#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioCallbackRuntime.h"
#include "Audio/Asio/AsioOutputBackend.h"
#include "Audio/Asio/AsioSampleConverter.h"
#include "Audio/Mixer/AudioRenderCore.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

namespace gc::audio::detail {

inline constexpr DWORD kAsioStartupClockDeadlineMs = 2'000;
inline constexpr DWORD kAsioRuntimeSummaryIntervalMs = 30'000;

struct AsioRenderDiagnosticsSnapshot
{
    std::uint64_t no_active_voice_silence_blocks{};
    std::uint64_t active_short_read_blocks{};
    std::uint64_t mixer_error_blocks{};
    std::uint64_t render_contract_error_blocks{};
    std::uint64_t short_read_missing_frames{};
    ma_result first_mixer_error{MA_SUCCESS};
    std::uint64_t clipped_output_blocks{};
    std::uint64_t clipped_output_samples{};
    std::uint64_t zero_output_blocks_with_active_voice{};
    std::uint64_t zero_output_blocks_without_active_voice{};
    std::uint64_t non_finite_output_blocks{};
    float maximum_absolute_output_sample{};
};

class AsioRenderDiagnostics final
{
public:
    void RecordRender(const AudioRenderBlock&) noexcept;
    void RecordConversion(
        const AudioRenderBlock&,
        const AsioStereoConversionResult&) noexcept;
    [[nodiscard]] AsioRenderDiagnosticsSnapshot Snapshot() const noexcept;

private:
    std::atomic_uint64_t no_active_voice_silence_blocks_{};
    std::atomic_uint64_t active_short_read_blocks_{};
    std::atomic_uint64_t mixer_error_blocks_{};
    std::atomic_uint64_t render_contract_error_blocks_{};
    std::atomic_uint64_t short_read_missing_frames_{};
    std::atomic_int32_t first_mixer_error_{MA_SUCCESS};
    std::atomic_uint64_t clipped_output_blocks_{};
    std::atomic_uint64_t clipped_output_samples_{};
    std::atomic_uint64_t zero_output_blocks_with_active_voice_{};
    std::atomic_uint64_t zero_output_blocks_without_active_voice_{};
    std::atomic_uint64_t non_finite_output_blocks_{};
    std::atomic_uint32_t maximum_absolute_output_sample_bits_{};
};

struct AsioOutputBackendActions
{
    void* context{};
    HRESULT (*initialize_com)(
        void* context,
        DWORD coinit_flags) noexcept{};
    void (*uninitialize_com)(void* context) noexcept{};
    HANDLE (*create_manual_event)(void* context) noexcept{};
    bool (*signal_event)(void* context, HANDLE event) noexcept{};
    DWORD (*wait_for_event)(
        void* context,
        HANDLE event,
        DWORD timeout_ms) noexcept{};
    void (*close_handle)(void* context, HANDLE handle) noexcept{};
    DWORD (*message_wait)(
        void* context,
        std::span<const HANDLE> handles,
        DWORD timeout_ms) noexcept{};
    void (*drain_messages)(void* context) noexcept{};
    std::uint64_t (*tick_count_ms)(void* context) noexcept{};
    std::uint32_t (*time_get_time_ms)(void* context) noexcept{};
    AsioCallbackRuntimeActions callback_runtime_actions{};
    DWORD summary_interval_ms{kAsioRuntimeSummaryIntervalMs};
};

[[nodiscard]] AsioOutputBackendActions
ProductionAsioOutputBackendActions() noexcept;

[[nodiscard]] std::unique_ptr<AsioOutputBackend>
StartAsioOutputBackendAndWait(
    HWND game_window,
    const AsioStreamRequest&,
    std::unique_ptr<IAsioRegistrySource>,
    std::unique_ptr<IAsioDriverFactory>,
    std::shared_ptr<IAsioOutputObserver>,
    std::shared_ptr<const ma_allocation_callbacks>,
    DWORD startup_clock_timeout_ms,
    const AsioOutputBackendActions&,
    AsioFailure*) noexcept;

} // namespace gc::audio::detail
