#include "Audio/Asio/AsioPhysicalSessionController.h"

namespace gc::audio
{
    namespace
    {
        AsioControlDirective Directive(
            const AsioControlDirectiveKind kind,
            const std::uint32_t recovery_attempt,
            const std::uint32_t retry_delay_ms = 0) noexcept
        {
            return {
                .kind = kind,
                .recovery_attempt = recovery_attempt,
                .retry_delay_ms = retry_delay_ms,
            };
        }
    }

    AsioControlDirective AsioPhysicalSessionController::Start(
        const AsioForegroundSnapshot& foreground) noexcept
    {
        if (started_)
        {
            return ProtocolFailure();
        }

        started_ = true;
        desired_foreground_ = foreground.is_foreground;
        consumed_focus_loss_generation_ = foreground.loss_generation;
        if (!desired_foreground_)
        {
            state_ = AsioLifecycleState::Suspended;
            return Directive(
                AsioControlDirectiveKind::ContinuePump,
                recovery_attempt_);
        }
        return BeginPhysicalAttempt();
    }

    AsioControlDirective AsioPhysicalSessionController::ObserveForeground(
        const AsioForegroundSnapshot& foreground) noexcept
    {
        if (!started_)
        {
            return ProtocolFailure();
        }
        if (state_ == AsioLifecycleState::Fatal)
        {
            return Directive(
                AsioControlDirectiveKind::FailFatal,
                recovery_attempt_);
        }
        if (state_ == AsioLifecycleState::Stopping)
        {
            return Directive(
                AsioControlDirectiveKind::Stop,
                recovery_attempt_);
        }
        if (foreground.loss_generation <
            consumed_focus_loss_generation_)
        {
            return ProtocolFailure();
        }

        const bool observed_loss =
            foreground.loss_generation >
            consumed_focus_loss_generation_ ||
            !foreground.is_foreground;
        desired_foreground_ = foreground.is_foreground;
        if (foreground.loss_generation >
            consumed_focus_loss_generation_)
        {
            consumed_focus_loss_generation_ =
                foreground.loss_generation;
            recovery_attempt_ = 0;
            retry_waiting_ = false;
        }

        if (observed_loss)
        {
            if (state_ == AsioLifecycleState::Suspended &&
                !release_pending_)
            {
                if (desired_foreground_)
                {
                    return BeginPhysicalAttempt();
                }
                return Directive(
                    AsioControlDirectiveKind::ContinuePump,
                    recovery_attempt_);
            }

            state_ = AsioLifecycleState::Suspended;
            attempt_in_progress_ = false;
            retry_waiting_ = false;
            release_pending_ = true;
            recovery_attempt_ = 0;
            return Directive(
                AsioControlDirectiveKind::ReleaseToSuspended,
                recovery_attempt_);
        }

        if (state_ == AsioLifecycleState::Suspended &&
            !release_pending_)
        {
            return BeginPhysicalAttempt();
        }
        return Directive(
            AsioControlDirectiveKind::ContinuePump,
            recovery_attempt_);
    }

    AsioControlDirective
    AsioPhysicalSessionController::ReportPhysicalReleased() noexcept
    {
        if (!started_ ||
            state_ != AsioLifecycleState::Suspended ||
            !release_pending_)
        {
            return ProtocolFailure();
        }

        release_pending_ = false;
        attempt_in_progress_ = false;
        retry_waiting_ = false;
        commit_phase_ = AsioPhysicalCommitPhase::None;
        if (desired_foreground_)
        {
            return BeginPhysicalAttempt();
        }
        return Directive(
            AsioControlDirectiveKind::ContinuePump,
            recovery_attempt_);
    }

    AsioControlDirective
    AsioPhysicalSessionController::ReportPrepared() noexcept
    {
        if (!attempt_in_progress_ ||
            (state_ != AsioLifecycleState::Starting &&
                state_ != AsioLifecycleState::Recovering) ||
            commit_phase_ != AsioPhysicalCommitPhase::None)
        {
            return ProtocolFailure();
        }

        ever_prepared_ = true;
        commit_phase_ = AsioPhysicalCommitPhase::Prepared;
        return Directive(
            AsioControlDirectiveKind::ContinuePump,
            recovery_attempt_);
    }

    AsioControlDirective
    AsioPhysicalSessionController::ReportPrimingStarted() noexcept
    {
        if (!attempt_in_progress_ ||
            commit_phase_ != AsioPhysicalCommitPhase::Prepared)
        {
            return ProtocolFailure();
        }

        commit_phase_ = AsioPhysicalCommitPhase::Priming;
        return Directive(
            AsioControlDirectiveKind::ContinuePump,
            recovery_attempt_);
    }

    AsioControlDirective
    AsioPhysicalSessionController::ReportRenderLeaseTransferred() noexcept
    {
        if (!attempt_in_progress_ ||
            commit_phase_ != AsioPhysicalCommitPhase::Priming)
        {
            return ProtocolFailure();
        }

        commit_phase_ =
            AsioPhysicalCommitPhase::RenderLeaseTransferred;
        return Directive(
            AsioControlDirectiveKind::ContinuePump,
            recovery_attempt_);
    }

    AsioControlDirective
    AsioPhysicalSessionController::ReportRunningCommitted() noexcept
    {
        if (!attempt_in_progress_ ||
            commit_phase_ !=
            AsioPhysicalCommitPhase::RenderLeaseTransferred)
        {
            return ProtocolFailure();
        }

        attempt_in_progress_ = false;
        retry_waiting_ = false;
        state_ = AsioLifecycleState::Running;
        commit_phase_ = AsioPhysicalCommitPhase::Running;
        return Directive(
            AsioControlDirectiveKind::CommitRunning,
            recovery_attempt_);
    }

    AsioControlDirective
    AsioPhysicalSessionController::ReportAttemptFailed(
        const AsioPhysicalAttemptFailureKind failure_kind,
        const bool cleanup_complete) noexcept
    {
        if (!attempt_in_progress_ ||
            (state_ != AsioLifecycleState::Starting &&
                state_ != AsioLifecycleState::Recovering) ||
            commit_phase_ == AsioPhysicalCommitPhase::Running)
        {
            return ProtocolFailure();
        }

        attempt_in_progress_ = false;
        commit_phase_ = AsioPhysicalCommitPhase::None;
        if (state_ == AsioLifecycleState::Starting ||
            failure_kind == AsioPhysicalAttemptFailureKind::Fatal ||
            !cleanup_complete)
        {
            state_ = AsioLifecycleState::Fatal;
            retry_waiting_ = false;
            return Directive(
                AsioControlDirectiveKind::FailFatal,
                recovery_attempt_);
        }
        if (!desired_foreground_)
        {
            state_ = AsioLifecycleState::Suspended;
            recovery_attempt_ = 0;
            retry_waiting_ = false;
            return Directive(
                AsioControlDirectiveKind::ContinuePump,
                recovery_attempt_);
        }
        if (recovery_attempt_ == 0 ||
            recovery_attempt_ >= kMaximumRecoveryAttempts)
        {
            state_ = AsioLifecycleState::Fatal;
            retry_waiting_ = false;
            return Directive(
                AsioControlDirectiveKind::FailFatal,
                recovery_attempt_);
        }

        const auto delay =
            kRecoveryRetryDelaysMs[
                static_cast<std::size_t>(recovery_attempt_ - 1)];
        retry_waiting_ = true;
        return Directive(
            AsioControlDirectiveKind::WaitRetry,
            recovery_attempt_,
            delay);
    }

    AsioControlDirective
    AsioPhysicalSessionController::ReportRetryDelayElapsed() noexcept
    {
        if (state_ != AsioLifecycleState::Recovering ||
            !retry_waiting_ ||
            !desired_foreground_ ||
            recovery_attempt_ == 0 ||
            recovery_attempt_ >= kMaximumRecoveryAttempts)
        {
            return ProtocolFailure();
        }

        retry_waiting_ = false;
        ++recovery_attempt_;
        attempt_in_progress_ = true;
        commit_phase_ = AsioPhysicalCommitPhase::None;
        return Directive(
            AsioControlDirectiveKind::BeginPhysicalAttempt,
            recovery_attempt_);
    }

    AsioControlDirective
    AsioPhysicalSessionController::ReportRuntimeFault() noexcept
    {
        if (state_ != AsioLifecycleState::Running ||
            commit_phase_ != AsioPhysicalCommitPhase::Running)
        {
            return ProtocolFailure();
        }

        state_ = AsioLifecycleState::Fatal;
        return Directive(
            AsioControlDirectiveKind::FailFatal,
            recovery_attempt_);
    }

    AsioControlDirective
    AsioPhysicalSessionController::RequestShutdown() noexcept
    {
        state_ = AsioLifecycleState::Stopping;
        attempt_in_progress_ = false;
        retry_waiting_ = false;
        release_pending_ = false;
        return Directive(
            AsioControlDirectiveKind::Stop,
            recovery_attempt_);
    }

    AsioLifecycleState AsioPhysicalSessionController::state() const noexcept
    {
        return state_;
    }

    AsioPhysicalCommitPhase
    AsioPhysicalSessionController::commit_phase() const noexcept
    {
        return commit_phase_;
    }

    bool AsioPhysicalSessionController::desired_foreground() const noexcept
    {
        return desired_foreground_;
    }

    std::uint64_t
    AsioPhysicalSessionController::consumed_focus_loss_generation()
    const noexcept
    {
        return consumed_focus_loss_generation_;
    }

    std::uint32_t
    AsioPhysicalSessionController::recovery_attempt() const noexcept
    {
        return recovery_attempt_;
    }

    AsioControlDirective
    AsioPhysicalSessionController::BeginPhysicalAttempt() noexcept
    {
        if (!started_ ||
            !desired_foreground_ ||
            release_pending_ ||
            retry_waiting_ ||
            attempt_in_progress_)
        {
            return ProtocolFailure();
        }

        commit_phase_ = AsioPhysicalCommitPhase::None;
        attempt_in_progress_ = true;
        if (ever_prepared_)
        {
            state_ = AsioLifecycleState::Recovering;
            if (recovery_attempt_ == 0)
            {
                recovery_attempt_ = 1;
            }
        }
        else
        {
            state_ = AsioLifecycleState::Starting;
            recovery_attempt_ = 0;
        }
        return Directive(
            AsioControlDirectiveKind::BeginPhysicalAttempt,
            recovery_attempt_);
    }

    AsioControlDirective
    AsioPhysicalSessionController::ProtocolFailure() noexcept
    {
        state_ = AsioLifecycleState::Fatal;
        attempt_in_progress_ = false;
        retry_waiting_ = false;
        release_pending_ = false;
        return Directive(
            AsioControlDirectiveKind::FailFatal,
            recovery_attempt_);
    }
} // namespace gc::audio
