#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioTypes.h"
#include "Audio/DirectSound/DirectSoundFacade.h"
#include "Audio/Mixer/AudioRenderCore.h"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

namespace gc::audio
{
    class AsioOutputBackend final : public IAudioEngineServices
    {
    public:
        static std::unique_ptr<AsioOutputBackend> Start(
            HWND game_window,
            const AsioStreamRequest&,
            std::unique_ptr<IAsioRegistrySource>,
            std::unique_ptr<IAsioDriverFactory>,
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
        struct PublishedServices final
        {
            AudioRenderCore* render_core{};
            std::uint32_t endpoint_buffer_frames{};
            std::uint32_t output_sample_rate{};
        };

        AsioOutputBackend() = default;

        static void OwnerThreadMain(
            HWND game_window,
            AsioStreamRequest request,
            std::unique_ptr<IAsioRegistrySource> registry,
            std::unique_ptr<IAsioDriverFactory> driver_factory,
            std::shared_ptr<const ma_allocation_callbacks> allocation_callbacks,
            HANDLE startup_complete,
            HANDLE shutdown_requested,
            PublishedServices* published_services) noexcept;

        std::thread owner_thread_;
        HANDLE startup_complete_{};
        HANDLE shutdown_requested_{};
        PublishedServices services_{};
    };
} // namespace gc::audio
