#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioTypes.h"

#include <Windows.h>

#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <variant>

namespace gc::audio
{
    struct AsioAdoptCurrentRate final
    {
    };

    struct AsioRequireFrozenRate final
    {
        std::uint32_t sample_rate{};
    };

    using AsioSampleRatePolicy =
    std::variant<AsioAdoptCurrentRate, AsioRequireFrozenRate>;

    struct AsioSessionPreparationFailure final
    {
        AsioFailure failure;
        bool cleanup_complete{};
    };

    struct AsioSessionCleanupReport final
    {
        bool stop_complete{};
        bool buffers_disposed{};
        bool sample_rate_restoration_attempted{};
        bool sample_rate_restored{};
    };

    class AsioSession final
    {
    public:
        static std::expected<std::unique_ptr<AsioSession>,
                             AsioSessionPreparationFailure>
        Prepare(
            AsioDriverRegistration registration,
            std::unique_ptr<IAsioDriver> driver,
            const AsioStreamRequest& request,
            HWND system_reference,
            AsioProbeMode mode,
            AsioSampleRatePolicy sample_rate_policy) noexcept;

        ~AsioSession();
        AsioSession(const AsioSession&) = delete;
        AsioSession& operator=(const AsioSession&) = delete;

        [[nodiscard]] std::expected<void, AsioFailure>
        CreateOutputBuffers(ASIOCallbacks* callbacks) noexcept;
        [[nodiscard]] std::expected<void, AsioFailure> Start() noexcept;
        [[nodiscard]] std::expected<void, AsioFailure> Stop() noexcept;
        [[nodiscard]] std::expected<void, AsioFailure> Close() noexcept;

        [[nodiscard]] const AsioCapabilityReport& report() const noexcept;
        [[nodiscard]] const AsioSessionCleanupReport&
        cleanup_report() const noexcept;
        [[nodiscard]] std::span<ASIOBufferInfo> buffers() noexcept;
        [[nodiscard]] IAsioDriver& driver() noexcept;

    private:
        AsioSession(
            AsioDriverRegistration registration,
            std::unique_ptr<IAsioDriver> driver,
            AsioSampleRatePolicy sample_rate_policy) noexcept;

        [[nodiscard]] std::expected<void, AsioFailure> PrepareDriver(
            const AsioStreamRequest& request,
            HWND system_reference,
            AsioProbeMode mode);
        [[nodiscard]] std::expected<void, AsioFailure>
        RequireCreatingThread() const;

        AsioCapabilityReport report_;
        std::unique_ptr<IAsioDriver> driver_;
        std::array<ASIOBufferInfo, 2> buffers_{};
        DWORD creating_thread_id_{};
        AsioSampleRatePolicy sample_rate_policy_;
        std::optional<ASIOSampleRate> sample_rate_to_restore_;
        AsioSessionCleanupReport cleanup_report_{};
        bool buffers_created_{};
        bool started_{};
        bool closed_{};
        std::optional<AsioFailure> cleanup_failure_;
    };
} // namespace gc::audio
