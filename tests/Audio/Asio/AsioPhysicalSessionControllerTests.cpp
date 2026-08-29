#include "Audio/Asio/AsioPhysicalSessionController.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    using gc::audio::AsioControlDirective;
    using gc::audio::AsioControlDirectiveKind;
    using gc::audio::AsioForegroundSnapshot;
    using gc::audio::AsioLifecycleState;
    using gc::audio::AsioPhysicalAttemptFailureKind;
    using gc::audio::AsioPhysicalCommitPhase;
    using gc::audio::AsioPhysicalSessionController;

    enum class Event : std::uint8_t
    {
        Start,
        ObserveForeground,
        PhysicalReleased,
        Prepared,
        PrimingStarted,
        RenderLeaseTransferred,
        RunningCommitted,
        RetryableFailure,
        RetryableFailureWithIncompleteCleanup,
        FatalFailure,
        RetryDelayElapsed,
        RuntimeFault,
        Shutdown,
    };

    struct Step final
    {
        Event event{};
        AsioForegroundSnapshot foreground{};
        AsioControlDirectiveKind directive{};
        AsioLifecycleState state{};
        AsioPhysicalCommitPhase phase{};
        std::uint32_t recovery_attempt{};
        std::uint32_t retry_delay_ms{};
        std::uint64_t consumed_loss_generation{};
    };

    constexpr Step Action(
        const Event event,
        const AsioControlDirectiveKind directive,
        const AsioLifecycleState state,
        const AsioPhysicalCommitPhase phase,
        const std::uint32_t recovery_attempt = 0,
        const std::uint32_t retry_delay_ms = 0,
        const std::uint64_t consumed_loss_generation = 0) noexcept
    {
        return {
            .event = event,
            .directive = directive,
            .state = state,
            .phase = phase,
            .recovery_attempt = recovery_attempt,
            .retry_delay_ms = retry_delay_ms,
            .consumed_loss_generation = consumed_loss_generation,
        };
    }

    constexpr Step Focus(
        const Event event,
        const bool foreground,
        const std::uint64_t loss_generation,
        const AsioControlDirectiveKind directive,
        const AsioLifecycleState state,
        const AsioPhysicalCommitPhase phase,
        const std::uint32_t recovery_attempt = 0,
        const std::uint32_t retry_delay_ms = 0,
        const std::uint64_t consumed_loss_generation = 0) noexcept
    {
        auto step = Action(
            event,
            directive,
            state,
            phase,
            recovery_attempt,
            retry_delay_ms,
            consumed_loss_generation);
        step.foreground = {
            .is_foreground = foreground,
            .loss_generation = loss_generation,
        };
        return step;
    }

    AsioControlDirective Apply(
        AsioPhysicalSessionController& controller,
        const Step& step) noexcept
    {
        switch (step.event)
        {
        case Event::Start:
            return controller.Start(step.foreground);
        case Event::ObserveForeground:
            return controller.ObserveForeground(step.foreground);
        case Event::PhysicalReleased:
            return controller.ReportPhysicalReleased();
        case Event::Prepared:
            return controller.ReportPrepared();
        case Event::PrimingStarted:
            return controller.ReportPrimingStarted();
        case Event::RenderLeaseTransferred:
            return controller.ReportRenderLeaseTransferred();
        case Event::RunningCommitted:
            return controller.ReportRunningCommitted();
        case Event::RetryableFailure:
            return controller.ReportAttemptFailed(
                AsioPhysicalAttemptFailureKind::RetryableBeforeRunning,
                true);
        case Event::RetryableFailureWithIncompleteCleanup:
            return controller.ReportAttemptFailed(
                AsioPhysicalAttemptFailureKind::RetryableBeforeRunning,
                false);
        case Event::FatalFailure:
            return controller.ReportAttemptFailed(
                AsioPhysicalAttemptFailureKind::Fatal,
                true);
        case Event::RetryDelayElapsed:
            return controller.ReportRetryDelayElapsed();
        case Event::RuntimeFault:
            return controller.ReportRuntimeFault();
        case Event::Shutdown:
            return controller.RequestShutdown();
        }
        return {};
    }

    template <std::size_t N>
    bool RunScenario(
        const std::string_view name,
        const std::array<Step, N>& steps)
    {
        // One controller must retain state across every step in the scenario.
        // ReSharper disable once CppTooWideScope
        AsioPhysicalSessionController controller;
        for (std::size_t index = 0; index < steps.size(); ++index)
        {
            const auto& expected = steps[index];
            const auto directive = Apply(controller, expected);
            if (directive.kind != expected.directive ||
                directive.recovery_attempt != expected.recovery_attempt ||
                directive.retry_delay_ms != expected.retry_delay_ms ||
                controller.state() != expected.state ||
                controller.commit_phase() != expected.phase ||
                controller.recovery_attempt() != expected.recovery_attempt ||
                controller.consumed_focus_loss_generation() !=
                expected.consumed_loss_generation)
            {
                std::cerr
                    << "FAIL: " << name << " step " << index
                    << " directive="
                    << static_cast<unsigned>(directive.kind)
                    << " state=" << static_cast<unsigned>(controller.state())
                    << " phase="
                    << static_cast<unsigned>(controller.commit_phase())
                    << " attempt=" << directive.recovery_attempt
                    << " delay=" << directive.retry_delay_ms
                    << " consumed="
                    << controller.consumed_focus_loss_generation()
                    << '\n';
                return false;
            }
        }
        return true;
    }

    constexpr auto kStartForeground = Focus(
        Event::Start,
        true,
        0,
        AsioControlDirectiveKind::BeginPhysicalAttempt,
        AsioLifecycleState::Starting,
        AsioPhysicalCommitPhase::None);
    constexpr auto kPreparedStarting = Action(
        Event::Prepared,
        AsioControlDirectiveKind::ContinuePump,
        AsioLifecycleState::Starting,
        AsioPhysicalCommitPhase::Prepared);
    constexpr auto kPrimingStarting = Action(
        Event::PrimingStarted,
        AsioControlDirectiveKind::ContinuePump,
        AsioLifecycleState::Starting,
        AsioPhysicalCommitPhase::Priming);
    constexpr auto kTransferredStarting = Action(
        Event::RenderLeaseTransferred,
        AsioControlDirectiveKind::ContinuePump,
        AsioLifecycleState::Starting,
        AsioPhysicalCommitPhase::RenderLeaseTransferred);
    constexpr auto kRunning = Action(
        Event::RunningCommitted,
        AsioControlDirectiveKind::CommitRunning,
        AsioLifecycleState::Running,
        AsioPhysicalCommitPhase::Running);
    constexpr auto kLostWhileRunning = Focus(
        Event::ObserveForeground,
        false,
        1,
        AsioControlDirectiveKind::ReleaseToSuspended,
        AsioLifecycleState::Suspended,
        AsioPhysicalCommitPhase::Running,
        0,
        0,
        1);
    constexpr auto kReleasedBackground = Action(
        Event::PhysicalReleased,
        AsioControlDirectiveKind::ContinuePump,
        AsioLifecycleState::Suspended,
        AsioPhysicalCommitPhase::None,
        0,
        0,
        1);
    constexpr auto kRegainedForRecovery = Focus(
        Event::ObserveForeground,
        true,
        1,
        AsioControlDirectiveKind::BeginPhysicalAttempt,
        AsioLifecycleState::Recovering,
        AsioPhysicalCommitPhase::None,
        1,
        0,
        1);
    constexpr auto kPreparedRecovery1 = Action(
        Event::Prepared,
        AsioControlDirectiveKind::ContinuePump,
        AsioLifecycleState::Recovering,
        AsioPhysicalCommitPhase::Prepared,
        1,
        0,
        1);
    constexpr auto kFailureWait1 = Action(
        Event::RetryableFailure,
        AsioControlDirectiveKind::WaitRetry,
        AsioLifecycleState::Recovering,
        AsioPhysicalCommitPhase::None,
        1,
        1'000,
        1);
    constexpr auto kRetry2 = Action(
        Event::RetryDelayElapsed,
        AsioControlDirectiveKind::BeginPhysicalAttempt,
        AsioLifecycleState::Recovering,
        AsioPhysicalCommitPhase::None,
        2,
        0,
        1);
    constexpr auto kPreparedRecovery2 = Action(
        Event::Prepared,
        AsioControlDirectiveKind::ContinuePump,
        AsioLifecycleState::Recovering,
        AsioPhysicalCommitPhase::Prepared,
        2,
        0,
        1);
    constexpr auto kFailureWait2 = Action(
        Event::RetryableFailure,
        AsioControlDirectiveKind::WaitRetry,
        AsioLifecycleState::Recovering,
        AsioPhysicalCommitPhase::None,
        2,
        2'000,
        1);
    constexpr auto kRetry3 = Action(
        Event::RetryDelayElapsed,
        AsioControlDirectiveKind::BeginPhysicalAttempt,
        AsioLifecycleState::Recovering,
        AsioPhysicalCommitPhase::None,
        3,
        0,
        1);
    constexpr auto kPreparedRecovery3 = Action(
        Event::Prepared,
        AsioControlDirectiveKind::ContinuePump,
        AsioLifecycleState::Recovering,
        AsioPhysicalCommitPhase::Prepared,
        3,
        0,
        1);

    constexpr std::array kFocusLossDuringInitialPreparation{
        kStartForeground,
        Focus(
            Event::ObserveForeground,
            false,
            1,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Starting,
            AsioPhysicalCommitPhase::None,
            0,
            0,
            1),
        Action(
            Event::Prepared,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Starting,
            AsioPhysicalCommitPhase::Prepared,
            0,
            0,
            1),
        Action(
            Event::PrimingStarted,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Starting,
            AsioPhysicalCommitPhase::Priming,
            0,
            0,
            1),
        Action(
            Event::RenderLeaseTransferred,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Starting,
            AsioPhysicalCommitPhase::RenderLeaseTransferred,
            0,
            0,
            1),
        Action(
            Event::RunningCommitted,
            AsioControlDirectiveKind::CommitRunning,
            AsioLifecycleState::Running,
            AsioPhysicalCommitPhase::Running,
            0,
            0,
            1),
        Focus(
            Event::ObserveForeground,
            false,
            1,
            AsioControlDirectiveKind::ReleaseToSuspended,
            AsioLifecycleState::Suspended,
            AsioPhysicalCommitPhase::Running,
            0,
            0,
            1),
        kReleasedBackground,
    };

    constexpr std::array kFocusLossDuringPriming{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        Focus(
            Event::ObserveForeground,
            false,
            1,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Starting,
            AsioPhysicalCommitPhase::Priming,
            0,
            0,
            1),
        Action(
            Event::RenderLeaseTransferred,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Starting,
            AsioPhysicalCommitPhase::RenderLeaseTransferred,
            0,
            0,
            1),
        Action(
            Event::RunningCommitted,
            AsioControlDirectiveKind::CommitRunning,
            AsioLifecycleState::Running,
            AsioPhysicalCommitPhase::Running,
            0,
            0,
            1),
        Focus(
            Event::ObserveForeground,
            false,
            1,
            AsioControlDirectiveKind::ReleaseToSuspended,
            AsioLifecycleState::Suspended,
            AsioPhysicalCommitPhase::Running,
            0,
            0,
            1),
        kReleasedBackground,
    };

    constexpr std::array kFocusLossAfterTransfer{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        Focus(
            Event::ObserveForeground,
            false,
            1,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Starting,
            AsioPhysicalCommitPhase::RenderLeaseTransferred,
            0,
            0,
            1),
        Action(
            Event::RunningCommitted,
            AsioControlDirectiveKind::CommitRunning,
            AsioLifecycleState::Running,
            AsioPhysicalCommitPhase::Running,
            0,
            0,
            1),
        Focus(
            Event::ObserveForeground,
            false,
            1,
            AsioControlDirectiveKind::ReleaseToSuspended,
            AsioLifecycleState::Suspended,
            AsioPhysicalCommitPhase::Running,
            0,
            0,
            1),
        kReleasedBackground,
    };

    constexpr std::array kInitiallyBackgroundCompletesStartup{
        Focus(
            Event::Start,
            false,
            0,
            AsioControlDirectiveKind::BeginPhysicalAttempt,
            AsioLifecycleState::Starting,
            AsioPhysicalCommitPhase::None),
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        Focus(
            Event::ObserveForeground,
            false,
            0,
            AsioControlDirectiveKind::ReleaseToSuspended,
            AsioLifecycleState::Suspended,
            AsioPhysicalCommitPhase::Running),
        Action(
            Event::PhysicalReleased,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Suspended,
            AsioPhysicalCommitPhase::None),
    };

    constexpr std::array kFocusLossWhileRunning{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        kLostWhileRunning,
        kReleasedBackground,
    };

    constexpr std::array kFocusLossDuringRecoveryPriming{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        kLostWhileRunning,
        kReleasedBackground,
        kRegainedForRecovery,
        kPreparedRecovery1,
        Action(
            Event::PrimingStarted,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Recovering,
            AsioPhysicalCommitPhase::Priming,
            1,
            0,
            1),
        Focus(
            Event::ObserveForeground,
            false,
            2,
            AsioControlDirectiveKind::ReleaseToSuspended,
            AsioLifecycleState::Suspended,
            AsioPhysicalCommitPhase::Priming,
            0,
            0,
            2),
        Action(
            Event::PhysicalReleased,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Suspended,
            AsioPhysicalCommitPhase::None,
            0,
            0,
            2),
    };

    constexpr std::array kFocusLossDuringRecoveryPreparation{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        kLostWhileRunning,
        kReleasedBackground,
        kRegainedForRecovery,
        Focus(
            Event::ObserveForeground,
            false,
            2,
            AsioControlDirectiveKind::ReleaseToSuspended,
            AsioLifecycleState::Suspended,
            AsioPhysicalCommitPhase::None,
            0,
            0,
            2),
        Action(
            Event::PhysicalReleased,
            AsioControlDirectiveKind::ContinuePump,
            AsioLifecycleState::Suspended,
            AsioPhysicalCommitPhase::None,
            0,
            0,
            2),
    };

    constexpr std::array kRecoveryRetrySchedule{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        kLostWhileRunning,
        kReleasedBackground,
        kRegainedForRecovery,
        kPreparedRecovery1,
        kFailureWait1,
        kRetry2,
        kPreparedRecovery2,
        kFailureWait2,
        kRetry3,
    };

    constexpr std::array kThirdRecoveryFailureIsFatal{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        kLostWhileRunning,
        kReleasedBackground,
        kRegainedForRecovery,
        kPreparedRecovery1,
        kFailureWait1,
        kRetry2,
        kPreparedRecovery2,
        kFailureWait2,
        kRetry3,
        kPreparedRecovery3,
        Action(
            Event::RetryableFailure,
            AsioControlDirectiveKind::FailFatal,
            AsioLifecycleState::Fatal,
            AsioPhysicalCommitPhase::None,
            3,
            0,
            1),
    };

    constexpr std::array kRunningFaultIsFatal{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        Action(
            Event::RuntimeFault,
            AsioControlDirectiveKind::FailFatal,
            AsioLifecycleState::Fatal,
            AsioPhysicalCommitPhase::Running),
    };

    constexpr std::array kIncompleteCleanupIsFatal{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        kLostWhileRunning,
        kReleasedBackground,
        kRegainedForRecovery,
        kPreparedRecovery1,
        Action(
            Event::RetryableFailureWithIncompleteCleanup,
            AsioControlDirectiveKind::FailFatal,
            AsioLifecycleState::Fatal,
            AsioPhysicalCommitPhase::None,
            1,
            0,
            1),
    };

    constexpr std::array kShutdownInterruptsFirstRetry{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        kLostWhileRunning,
        kReleasedBackground,
        kRegainedForRecovery,
        kPreparedRecovery1,
        kFailureWait1,
        Action(
            Event::Shutdown,
            AsioControlDirectiveKind::Stop,
            AsioLifecycleState::Stopping,
            AsioPhysicalCommitPhase::None,
            1,
            0,
            1),
    };

    constexpr std::array kShutdownInterruptsSecondRetry{
        kStartForeground,
        kPreparedStarting,
        kPrimingStarting,
        kTransferredStarting,
        kRunning,
        kLostWhileRunning,
        kReleasedBackground,
        kRegainedForRecovery,
        kPreparedRecovery1,
        kFailureWait1,
        kRetry2,
        kPreparedRecovery2,
        kFailureWait2,
        Action(
            Event::Shutdown,
            AsioControlDirectiveKind::Stop,
            AsioLifecycleState::Stopping,
            AsioPhysicalCommitPhase::None,
            2,
            0,
            1),
    };

    constexpr std::array kInitialFailureIsFatal{
        kStartForeground,
        Action(
            Event::RetryableFailure,
            AsioControlDirectiveKind::FailFatal,
            AsioLifecycleState::Fatal,
            AsioPhysicalCommitPhase::None),
    };

    static_assert(
        static_cast<std::uint8_t>(AsioControlDirectiveKind::Stop) + 1 ==
        gc::audio::kAsioControlDirectiveKindCount);
    static_assert(gc::audio::kAsioControlDirectiveKindCount == 7);
}

int main()
{
    const bool passed =
        RunScenario(
            "initially background completes startup before suspension",
            kInitiallyBackgroundCompletesStartup) &&
        RunScenario(
            "focus loss during initial preparation",
            kFocusLossDuringInitialPreparation) &&
        RunScenario(
            "focus loss during priming",
            kFocusLossDuringPriming) &&
        RunScenario(
            "focus loss after render transfer",
            kFocusLossAfterTransfer) &&
        RunScenario(
            "focus loss while running",
            kFocusLossWhileRunning) &&
        RunScenario(
            "focus loss during recovery priming",
            kFocusLossDuringRecoveryPriming) &&
        RunScenario(
            "focus loss during recovery preparation",
            kFocusLossDuringRecoveryPreparation) &&
        RunScenario(
            "recovery retry schedule",
            kRecoveryRetrySchedule) &&
        RunScenario(
            "third recovery failure",
            kThirdRecoveryFailureIsFatal) &&
        RunScenario(
            "running fault",
            kRunningFaultIsFatal) &&
        RunScenario(
            "incomplete recovery cleanup",
            kIncompleteCleanupIsFatal) &&
        RunScenario(
            "shutdown during first retry",
            kShutdownInterruptsFirstRetry) &&
        RunScenario(
            "shutdown during second retry",
            kShutdownInterruptsSecondRetry) &&
        RunScenario(
            "initial failure",
            kInitialFailureIsFatal);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
