#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioForegroundState.h"

#include <array>
#include <cstdint>

namespace gc::audio
{
    enum class AsioLifecycleState : std::uint8_t
    {
        Starting,
        Running,
        Suspended,
        Recovering,
        Fatal,
        Stopping,
    };

    enum class AsioPhysicalCommitPhase : std::uint8_t
    {
        None,
        Prepared,
        Priming,
        RenderLeaseTransferred,
        Running,
    };

    enum class AsioControlDirectiveKind : std::uint8_t
    {
        ContinuePump,
        BeginPhysicalAttempt,
        ReleaseToSuspended,
        WaitRetry,
        CommitRunning,
        FailFatal,
        Stop,
    };

    inline constexpr std::array<std::uint32_t, 2> kRecoveryRetryDelaysMs{
        1'000,
        2'000,
    };
    inline constexpr std::uint32_t kMaximumRecoveryAttempts = 3;
    inline constexpr std::uint8_t kAsioControlDirectiveKindCount = 7;

    enum class AsioPhysicalAttemptFailureKind : std::uint8_t
    {
        RetryableBeforeRunning,
        Fatal,
    };

    struct AsioControlDirective final
    {
        AsioControlDirectiveKind kind{
            AsioControlDirectiveKind::ContinuePump
        };
        std::uint32_t recovery_attempt{};
        std::uint32_t retry_delay_ms{};
    };

    class AsioPhysicalSessionController final
    {
    public:
        [[nodiscard]] AsioControlDirective Start(
            const AsioForegroundSnapshot&) noexcept;
        [[nodiscard]] AsioControlDirective ObserveForeground(
            const AsioForegroundSnapshot&) noexcept;
        [[nodiscard]] AsioControlDirective ReportPhysicalReleased() noexcept;
        [[nodiscard]] AsioControlDirective ReportPrepared() noexcept;
        [[nodiscard]] AsioControlDirective ReportPrimingStarted() noexcept;
        [[nodiscard]] AsioControlDirective
        ReportRenderLeaseTransferred() noexcept;
        [[nodiscard]] AsioControlDirective ReportRunningCommitted() noexcept;
        [[nodiscard]] AsioControlDirective ReportAttemptFailed(
            AsioPhysicalAttemptFailureKind,
            bool cleanup_complete) noexcept;
        [[nodiscard]] AsioControlDirective ReportRetryDelayElapsed() noexcept;
        [[nodiscard]] AsioControlDirective ReportRuntimeFault() noexcept;
        [[nodiscard]] AsioControlDirective RequestShutdown() noexcept;

        [[nodiscard]] AsioLifecycleState state() const noexcept;
        [[nodiscard]] AsioPhysicalCommitPhase commit_phase() const noexcept;
        [[nodiscard]] bool desired_foreground() const noexcept;
        [[nodiscard]] std::uint64_t
        consumed_focus_loss_generation() const noexcept;
        [[nodiscard]] std::uint32_t recovery_attempt() const noexcept;

    private:
        [[nodiscard]] AsioControlDirective BeginPhysicalAttempt() noexcept;
        [[nodiscard]] AsioControlDirective ProtocolFailure() noexcept;

        AsioLifecycleState state_{AsioLifecycleState::Starting};
        AsioPhysicalCommitPhase commit_phase_{
            AsioPhysicalCommitPhase::None
        };
        bool desired_foreground_{};
        bool started_{};
        bool ever_prepared_{};
        bool attempt_in_progress_{};
        bool release_pending_{};
        bool retry_waiting_{};
        std::uint64_t consumed_focus_loss_generation_{};
        std::uint32_t recovery_attempt_{};
    };
} // namespace gc::audio
