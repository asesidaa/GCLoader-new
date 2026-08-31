#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioTypes.h"
#include "Audio/DirectSound/DirectSoundFacade.h"
#include "Audio/Mixer/AudioRenderCore.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace gc::audio
{
    class AsioOutputBackend final : public IAudioEngineServices
    {
    public:
        static std::unique_ptr<AsioOutputBackend> Start(
            HWND game_window,
            const AsioStreamRequest&,
            IAsioRegistrySource&,
            IAsioDriverFactory&,
            std::shared_ptr<const ma_allocation_callbacks> = {}) noexcept;

        ~AsioOutputBackend() override;

        AsioOutputBackend(const AsioOutputBackend&) = delete;
        AsioOutputBackend& operator=(const AsioOutputBackend&) = delete;

        std::unique_ptr<MixerVoice> CreateVoice(
            const NormalizedSourceFormat&,
            std::shared_ptr<AudioSnapshot>,
            std::shared_ptr<AudioCursorTimeline>,
            VoiceUsage,
            ma_result*) noexcept override;
        [[nodiscard]] std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
        [[nodiscard]] std::uint32_t endpoint_buffer_frames() const noexcept override;
        [[nodiscard]] std::uint32_t output_sample_rate() const noexcept override;
        void CountPendingCursorQuery() noexcept override;
        void CountUnmappedCursorFailure() noexcept override;

    private:
        struct FrozenFormat final
        {
            std::uint32_t sample_rate{};
            std::uint32_t frame_count{};
            std::array<std::uint32_t, 2> channels{};
            std::array<ASIOSampleType, 2> sample_types{};
        };

        AsioOutputBackend() = default;

        [[nodiscard]] std::span<const float> RenderPcm(
            std::uint32_t frame_count) noexcept;
        void FillBuffer(long buffer_index) noexcept;

        [[nodiscard]] static AsioOutputBackend* CallbackTarget() noexcept;
        static void BufferSwitch(
            long buffer_index,
            ASIOBool direct_process) noexcept;
        static void SampleRateDidChange(ASIOSampleRate sample_rate) noexcept;
        static long AsioMessage(
            long selector,
            long value,
            void* message,
            double* optional) noexcept;
        static ASIOTime* BufferSwitchTimeInfo(
            ASIOTime* time_info,
            long buffer_index,
            ASIOBool direct_process) noexcept;

        static const ASIOCallbacks callbacks_;
        static std::atomic<AsioOutputBackend*> callback_target_;
        static std::atomic_flag callback_active_;

        std::unique_ptr<IAsioDriver> driver_;
        std::array<ASIOBufferInfo, 2> buffers_{};
        FrozenFormat format_{};
        std::unique_ptr<AudioRenderCore> render_core_;
        std::array<std::vector<std::byte>, 2> conversion_storage_;
        bool output_ready_supported_{};
    };
} // namespace gc::audio
