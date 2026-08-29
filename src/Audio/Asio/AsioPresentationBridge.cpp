#include "Audio/Asio/AsioPresentationBridge.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr std::uint64_t kChannels = 2;
        constexpr std::uint64_t kNanosecondsPerSecond =
            1'000'000'000;
        constexpr double kPpmScale = 1'000'000.0;
        constexpr double kPrimingAlignmentToleranceFrames = 1.0;
        constexpr std::uint32_t kMaximumPrimingDiscardPeriods = 6;

        static_assert(std::atomic_bool::is_always_lock_free);
        static_assert(std::atomic_uint8_t::is_always_lock_free);
        static_assert(std::atomic_uint64_t::is_always_lock_free);

        [[nodiscard]] bool CheckedAdd(
            const std::uint64_t left,
            const std::uint64_t right,
            std::uint64_t* result) noexcept
        {
            if (result == nullptr ||
                right >
                (std::numeric_limits<std::uint64_t>::max)() -
                left)
            {
                return false;
            }
            *result = left + right;
            return true;
        }

        [[nodiscard]] bool CheckedMultiply(
            const std::uint64_t left,
            const std::uint64_t right,
            std::uint64_t* result) noexcept
        {
            if (result == nullptr ||
                (left != 0 &&
                    right >
                    (std::numeric_limits<std::uint64_t>::max)() /
                    left))
            {
                return false;
            }
            *result = left * right;
            return true;
        }

        [[nodiscard]] bool ComputePhaseEnvelope(
            const AsioPresentationBridgeConfig& config,
            const std::uint64_t resampler_input_latency,
            std::uint64_t* result) noexcept
        {
            std::uint64_t four_periods{};
            if (!CheckedMultiply(
                config.period_frames, 4, &four_periods))
            {
                return false;
            }
            const std::uint64_t minimum_time_frames =
                (static_cast<std::uint64_t>(config.logical_rate) *
                    kMinimumPhaseEnvelopeMs +
                    999) /
                1'000;
            const auto base =
                (std::max)(four_periods, minimum_time_frames);

            std::uint64_t quantum_numerator{};
            if (!CheckedMultiply(
                config.logical_rate,
                config.timestamp_quantum_ns,
                &quantum_numerator))
            {
                return false;
            }
            const auto quantum_frames =
                (quantum_numerator + kNanosecondsPerSecond - 1) /
                kNanosecondsPerSecond;
            std::uint64_t doubled_quantum{};
            std::uint64_t with_quantum{};
            return CheckedMultiply(
                    quantum_frames, 2, &doubled_quantum) &&
                CheckedAdd(
                    base, doubled_quantum, &with_quantum) &&
                CheckedAdd(
                    with_quantum,
                    resampler_input_latency,
                    result);
        }

        void StoreDouble(
            std::atomic_uint64_t& destination,
            const double value) noexcept
        {
            destination.store(
                std::bit_cast<std::uint64_t>(value),
                std::memory_order_release);
        }

        [[nodiscard]] double LoadDouble(
            const std::atomic_uint64_t& source) noexcept
        {
            return std::bit_cast<double>(
                source.load(std::memory_order_acquire));
        }

        [[nodiscard]] bool RationalToDouble(
            const gc::timing::CheckedRational& value,
            double* result) noexcept
        {
            if (result == nullptr || value.numerator() < 0 ||
                value.denominator() == 0)
            {
                return false;
            }

            constexpr auto maximum_exact_integer =
                std::uint64_t{1} << 52U;
            const auto numerator =
                static_cast<std::uint64_t>(value.numerator());
            if (numerator / value.denominator() >
                maximum_exact_integer)
            {
                return false;
            }

            *result =
                static_cast<double>(value.numerator()) /
                static_cast<double>(value.denominator());
            return std::isfinite(*result);
        }
    } // namespace

    AsioPresentationBridge::AsioPresentationBridge(
        const AsioPresentationBridgeConfig& config,
        std::shared_ptr<const LogicalPresentationClock>
        logical_clock,
        LogicalRenderStream& logical_render_stream,
        std::unique_ptr<AsioPresentationRateMatcher>
        rate_matcher,
        const std::uint64_t phase_envelope_frames) noexcept
        : config_(config),
          logical_clock_(std::move(logical_clock)),
          logical_render_stream_(logical_render_stream),
          rate_matcher_(std::move(rate_matcher)),
          phase_envelope_frames_(phase_envelope_frames)
    {
        physical_clock_.Reset(
            config_.period_frames,
            config_.driver_output_latency_frames);
        StoreDouble(initial_phase_error_bits_, 0.0);
        StoreDouble(maximum_absolute_phase_error_bits_, 0.0);
        StoreDouble(final_phase_error_bits_, 0.0);
        StoreDouble(minimum_rate_ratio_ppm_bits_, 0.0);
        StoreDouble(maximum_rate_ratio_ppm_bits_, 0.0);
        StoreDouble(final_rate_ratio_ppm_bits_, 0.0);
    }

    std::unique_ptr<AsioPresentationBridge>
    AsioPresentationBridge::Create(
        const AsioPresentationBridgeConfig& config,
        std::shared_ptr<const LogicalPresentationClock>
        logical_clock,
        LogicalRenderStream& logical_render_stream,
        std::shared_ptr<const ma_allocation_callbacks>
        allocation_callbacks) noexcept
    {
        if (config.physical_session_generation == 0 ||
            config.logical_rate == 0 ||
            config.driver_rate != config.logical_rate ||
            config.period_frames == 0 ||
            config.timestamp_quantum_ns == 0 ||
            logical_clock == nullptr)
        {
            return {};
        }
        const auto clock_info = logical_clock->info();
        if (clock_info.timeline_generation == 0 ||
            clock_info.logical_output_rate != config.logical_rate)
        {
            return {};
        }

        auto matcher_result =
            AsioPresentationRateMatcher::Create(
                config.logical_rate,
                config.driver_rate,
                config.period_frames,
                std::move(allocation_callbacks));
        if (!matcher_result)
        {
            return {};
        }
        std::uint64_t phase_envelope{};
        if (!ComputePhaseEnvelope(
            config,
            (*matcher_result)->input_latency_frames(),
            &phase_envelope))
        {
            return {};
        }

        return std::unique_ptr < AsioPresentationBridge >
        {
            new(std::nothrow) AsioPresentationBridge(
                config,
                std::move(logical_clock),
                logical_render_stream,
                std::move(*matcher_result),
                phase_envelope)
        };
    }

    bool AsioPresentationBridge::TryAcquireClaim() noexcept
    {
        bool expected = false;
        return process_claim_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    void AsioPresentationBridge::ReleaseClaim() noexcept
    {
        process_claim_.store(false, std::memory_order_release);
    }

    std::expected<void, AsioPresentationBridgeControlFailure>
    AsioPresentationBridge::Arm(
        const LogicalRenderLease& lease,
        const std::uint64_t exact_tail) noexcept
    {
        if (!TryAcquireClaim())
        {
            return std::unexpected(
                AsioPresentationBridgeControlFailure::Busy);
        }

        const auto current_state = state();
        if (current_state !=
            AsioPresentationBridgeState::Priming)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioPresentationBridgeControlFailure::InvalidState);
        }
        if (lease.owner != LogicalRenderOwner::AsioBridge ||
            lease.generation == 0 ||
            lease.acquired_tail != exact_tail)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioPresentationBridgeControlFailure::InvalidLease);
        }
        if (logical_render_stream_.committed_tail() != exact_tail)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioPresentationBridgeControlFailure::TailMismatch);
        }

        const auto reset = rate_matcher_->Reset();
        if (!reset)
        {
            LatchFault(
                AsioPresentationBridgeFault::RateControlFailure);
            ReleaseClaim();
            return std::unexpected(
                AsioPresentationBridgeControlFailure::
                MatcherResetFailed);
        }

        lease_ = lease;
        source_phase_ = static_cast<double>(exact_tail);
        current_rate_ratio_ = 1.0;
        phase_filter_.fill(0.0);
        phase_filter_count_ = 0;
        phase_filter_index_ = 0;
        phase_filter_sum_ = 0.0;
        has_phase_error_ = false;
        initial_phase_error_ = 0.0;
        maximum_absolute_phase_error_ = 0.0;
        final_phase_error_ = 0.0;
        minimum_rate_ratio_ppm_ = 0.0;
        maximum_rate_ratio_ppm_ = 0.0;
        final_rate_ratio_ppm_ = 0.0;
        handoff_logical_tail_.store(
            exact_tail, std::memory_order_release);
        state_.store(
            static_cast<std::uint8_t>(
                AsioPresentationBridgeState::Armed),
            std::memory_order_release);
        ReleaseClaim();
        return {};
    }

    bool AsioPresentationBridge::BeginQuiescing() noexcept
    {
        if (!TryAcquireClaim())
        {
            return false;
        }
        const auto current = state();
        if (current != AsioPresentationBridgeState::Quiescing &&
            current != AsioPresentationBridgeState::Faulted)
        {
            state_.store(
                static_cast<std::uint8_t>(
                    AsioPresentationBridgeState::Quiescing),
                std::memory_order_release);
        }
        ReleaseClaim();
        return true;
    }

    std::expected<std::optional<LogicalRenderLease>,
                  AsioPresentationBridgeControlFailure>
    AsioPresentationBridge::ReleaseLease(
        const std::uint64_t exact_tail) noexcept
    {
        if (!TryAcquireClaim())
        {
            return std::unexpected(
                AsioPresentationBridgeControlFailure::Busy);
        }
        const auto current = state();
        if (current != AsioPresentationBridgeState::Quiescing &&
            current != AsioPresentationBridgeState::Faulted)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioPresentationBridgeControlFailure::InvalidState);
        }
        if (logical_render_stream_.committed_tail() != exact_tail)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioPresentationBridgeControlFailure::TailMismatch);
        }

        auto result = lease_;
        lease_.reset();
        ReleaseClaim();
        return result;
    }

    void AsioPresentationBridge::LatchFault(
        const AsioPresentationBridgeFault fault) noexcept
    {
        if (fault == AsioPresentationBridgeFault::None)
        {
            return;
        }
        auto expected = static_cast<std::uint8_t>(
            AsioPresentationBridgeFault::None);
        first_fault_.compare_exchange_strong(
            expected,
            static_cast<std::uint8_t>(fault),
            std::memory_order_acq_rel,
            std::memory_order_acquire);
        state_.store(
            static_cast<std::uint8_t>(
                AsioPresentationBridgeState::Faulted),
            std::memory_order_release);
    }

    bool AsioPresentationBridge::ValidateAndProjectTarget(
        const AsioRenderRequest& request,
        double* target_source_phase) noexcept
    {
        if (target_source_phase == nullptr ||
            (request.buffer_index != 0 &&
                request.buffer_index != 1) ||
            !request.has_system_time)
        {
            LatchFault(
                AsioPresentationBridgeFault::InvalidCallback);
            return false;
        }
        const auto physical =
            physical_clock_.Observe(request.sample_position);
        if (physical.kind != AsioClockDecisionKind::valid ||
            (has_previous_system_time_ &&
                request.system_time_ns <
                previous_system_time_ns_))
        {
            LatchFault(
                AsioPresentationBridgeFault::InvalidClock);
            return false;
        }
        previous_system_time_ns_ = request.system_time_ns;
        has_previous_system_time_ = true;

        const auto logical =
            logical_clock_->ProjectSystemTimeNanoseconds(
                request.system_time_ns);
        double logical_frame{};
        if (!logical ||
            !RationalToDouble(*logical, &logical_frame))
        {
            LatchFault(
                AsioPresentationBridgeFault::InvalidClock);
            return false;
        }

        const double target =
            logical_frame +
            static_cast<double>(
                config_.driver_output_latency_frames) +
            static_cast<double>(
                rate_matcher_->input_latency_frames());
        if (!std::isfinite(target))
        {
            LatchFault(
                AsioPresentationBridgeFault::ArithmeticOverflow);
            return false;
        }
        *target_source_phase = target;
        return true;
    }

    bool AsioPresentationBridge::EnsureInputForOutput(
        const std::uint64_t output_frames) noexcept
    {
        const auto required =
            rate_matcher_->RequiredInputFrames(output_frames);
        if (!required)
        {
            conversion_failures_.fetch_add(
                1, std::memory_order_relaxed);
            LatchFault(
                AsioPresentationBridgeFault::ConversionFailure);
            return false;
        }
        if (*required >
            rate_matcher_->input_capacity_frames())
        {
            input_underflows_.fetch_add(
                1, std::memory_order_relaxed);
            LatchFault(
                AsioPresentationBridgeFault::InputStarvation);
            return false;
        }

        for (std::uint32_t block = 0;
             block <
             kMaximumPrimingRenderBlocksPerCallback &&
             rate_matcher_->buffered_input_frames() < *required;
             ++block)
        {
            if (!RenderOneLogicalBlock())
            {
                return false;
            }
        }
        if (rate_matcher_->buffered_input_frames() < *required)
        {
            input_underflows_.fetch_add(
                1, std::memory_order_relaxed);
            LatchFault(
                AsioPresentationBridgeFault::InputStarvation);
            return false;
        }
        return true;
    }

    bool AsioPresentationBridge::RenderOneLogicalBlock() noexcept
    {
        if (!lease_)
        {
            LatchFault(
                AsioPresentationBridgeFault::LostRenderLease);
            return false;
        }
        if (rate_matcher_->free_input_frames() <
            config_.period_frames)
        {
            input_overflows_.fetch_add(
                1, std::memory_order_relaxed);
            LatchFault(
                AsioPresentationBridgeFault::InputOverflow);
            return false;
        }

        const auto plan =
            logical_render_stream_.BeginRender(*lease_);
        if (!plan)
        {
            LatchFault(
                AsioPresentationBridgeFault::LostRenderLease);
            return false;
        }
        if (plan->timeline.discontinuity_frames != 0)
        {
            static_cast<void>(
                logical_render_stream_.Abandon(*plan));
            LatchFault(
                AsioPresentationBridgeFault::
                RenderDiscontinuity);
            return false;
        }

        const auto block =
            logical_render_stream_.Render(*plan);
        const auto expected_samples =
            static_cast<std::size_t>(config_.period_frames) *
            kChannels;
        if (block.mixer_result != MA_SUCCESS ||
            block.interleaved_stereo.size() !=
            expected_samples ||
            block.silence_reason ==
            AudioRenderSilenceReason::mixer_error ||
            block.silence_reason ==
            AudioRenderSilenceReason::
            render_contract_error)
        {
            LatchFault(
                AsioPresentationBridgeFault::RenderFailure);
            return false;
        }

        const auto pushed =
            rate_matcher_->Push(block.interleaved_stereo);
        if (!pushed)
        {
            if (pushed.error() ==
                AsioPresentationRateMatcherFailure::
                InputOverflow)
            {
                input_overflows_.fetch_add(
                    1, std::memory_order_relaxed);
                LatchFault(
                    AsioPresentationBridgeFault::
                    InputOverflow);
            }
            else
            {
                conversion_failures_.fetch_add(
                    1, std::memory_order_relaxed);
                LatchFault(
                    AsioPresentationBridgeFault::
                    ConversionFailure);
            }
            return false;
        }
        if (!logical_render_stream_.Commit(*plan))
        {
            LatchFault(
                AsioPresentationBridgeFault::
                RenderCommitFailure);
            return false;
        }

        logical_rendered_frames_.fetch_add(
            config_.period_frames,
            std::memory_order_relaxed);
        RecordInputHighWater();
        return true;
    }

    void AsioPresentationBridge::RecordInputHighWater() noexcept
    {
        const auto value =
            rate_matcher_->buffered_input_frames();
        auto observed = input_high_water_frames_.load(
            std::memory_order_relaxed);
        if (observed < value)
        {
            static_cast<void>(
                input_high_water_frames_.compare_exchange_strong(
                    observed,
                    value,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed));
        }
    }

    void AsioPresentationBridge::RecordPhaseError(
        const double phase_error_frames) noexcept
    {
        if (!has_phase_error_)
        {
            has_phase_error_ = true;
            initial_phase_error_ = phase_error_frames;
            maximum_absolute_phase_error_ =
                std::abs(phase_error_frames);
            StoreDouble(
                initial_phase_error_bits_,
                initial_phase_error_);
        }
        maximum_absolute_phase_error_ = (std::max)(
            maximum_absolute_phase_error_,
            std::abs(phase_error_frames));
        final_phase_error_ = phase_error_frames;
        StoreDouble(
            maximum_absolute_phase_error_bits_,
            maximum_absolute_phase_error_);
        StoreDouble(
            final_phase_error_bits_,
            final_phase_error_);
    }

    void AsioPresentationBridge::RecordRateRatio(
        const double rate_ratio) noexcept
    {
        const auto ppm = (rate_ratio - 1.0) * kPpmScale;
        minimum_rate_ratio_ppm_ =
            (std::min)(minimum_rate_ratio_ppm_, ppm);
        maximum_rate_ratio_ppm_ =
            (std::max)(maximum_rate_ratio_ppm_, ppm);
        final_rate_ratio_ppm_ = ppm;
        StoreDouble(
            minimum_rate_ratio_ppm_bits_,
            minimum_rate_ratio_ppm_);
        StoreDouble(
            maximum_rate_ratio_ppm_bits_,
            maximum_rate_ratio_ppm_);
        StoreDouble(
            final_rate_ratio_ppm_bits_,
            final_rate_ratio_ppm_);
    }

    bool AsioPresentationBridge::ApplyRateControl(
        const double phase_error_frames) noexcept
    {
        if (!std::isfinite(phase_error_frames))
        {
            LatchFault(
                AsioPresentationBridgeFault::ArithmeticOverflow);
            return false;
        }

        if (phase_filter_count_ < kPhaseFilterCallbacks)
        {
            phase_filter_[phase_filter_index_] =
                phase_error_frames;
            phase_filter_sum_ += phase_error_frames;
            ++phase_filter_count_;
        }
        else
        {
            phase_filter_sum_ -=
                phase_filter_[phase_filter_index_];
            phase_filter_[phase_filter_index_] =
                phase_error_frames;
            phase_filter_sum_ += phase_error_frames;
        }
        phase_filter_index_ =
            (phase_filter_index_ + 1) %
            kPhaseFilterCallbacks;

        const auto filtered =
            phase_filter_sum_ /
            static_cast<double>(phase_filter_count_);
        const auto horizon_frames =
            static_cast<double>(config_.logical_rate) *
            kPhaseCorrectionHorizonSeconds;
        constexpr auto maximum_correction =
            kMaximumRateCorrectionPpm / kPpmScale;
        const auto requested = std::clamp(
            1.0 + filtered / horizon_frames,
            1.0 - maximum_correction,
            1.0 + maximum_correction);
        constexpr auto maximum_slew =
            kMaximumRateSlewPpmPerCallback / kPpmScale;
        const auto slewed = std::clamp(
            requested,
            current_rate_ratio_ - maximum_slew,
            current_rate_ratio_ + maximum_slew);

        const auto actual =
            rate_matcher_->SetRateRatio(slewed);
        if (!actual)
        {
            conversion_failures_.fetch_add(
                1, std::memory_order_relaxed);
            LatchFault(
                AsioPresentationBridgeFault::
                RateControlFailure);
            return false;
        }
        current_rate_ratio_ = *actual;
        RecordRateRatio(current_rate_ratio_);
        return true;
    }

    bool AsioPresentationBridge::ProduceRunningPeriod(
        const double target_source_phase,
        const std::span<float> output,
        const bool commit_running) noexcept
    {
        const auto phase_error =
            target_source_phase - source_phase_;
        RecordPhaseError(phase_error);
        if (std::abs(phase_error) >
            static_cast<double>(phase_envelope_frames_))
        {
            phase_envelope_violations_.fetch_add(
                1, std::memory_order_relaxed);
            LatchFault(
                AsioPresentationBridgeFault::
                PhaseEnvelopeViolation);
            return false;
        }
        if (!ApplyRateControl(phase_error) ||
            !EnsureInputForOutput(config_.period_frames))
        {
            return false;
        }

        const auto converted = rate_matcher_->ProcessPeriod();
        if (!converted)
        {
            if (converted.error() ==
                AsioPresentationRateMatcherFailure::
                InputUnderflow)
            {
                input_underflows_.fetch_add(
                    1, std::memory_order_relaxed);
                LatchFault(
                    AsioPresentationBridgeFault::
                    InputStarvation);
            }
            else
            {
                conversion_failures_.fetch_add(
                    1, std::memory_order_relaxed);
                LatchFault(
                    AsioPresentationBridgeFault::
                    ConversionFailure);
            }
            return false;
        }
        if (converted->size() != output.size() ||
            !std::ranges::all_of(
                *converted,
                [](const float sample)
                {
                    return std::isfinite(sample);
                }))
        {
            non_finite_output_blocks_.fetch_add(
                1, std::memory_order_relaxed);
            LatchFault(
                AsioPresentationBridgeFault::
                NonFiniteOutput);
            return false;
        }

        std::ranges::copy(*converted, output.begin());
        source_phase_ +=
            current_rate_ratio_ * config_.period_frames;
        if (!std::isfinite(source_phase_))
        {
            LatchFault(
                AsioPresentationBridgeFault::ArithmeticOverflow);
            std::ranges::fill(output, 0.0F);
            return false;
        }

        if (commit_running)
        {
            state_.store(
                static_cast<std::uint8_t>(
                    AsioPresentationBridgeState::Running),
                std::memory_order_release);
        }
        running_callbacks_.fetch_add(
            1, std::memory_order_relaxed);
        return true;
    }

    bool AsioPresentationBridge::ProcessArmed(
        const double target_source_phase,
        const std::span<float> output) noexcept
    {
        if (source_phase_ >
            target_source_phase +
            kPrimingAlignmentToleranceFrames)
        {
            return false;
        }

        const auto difference =
            (std::max)(
                0.0,
                target_source_phase - source_phase_);
        if (!std::isfinite(difference) ||
            difference >
            static_cast<double>(
                (std::numeric_limits<std::uint64_t>::max)()))
        {
            LatchFault(
                AsioPresentationBridgeFault::ArithmeticOverflow);
            return false;
        }

        auto discard_frames =
            static_cast<std::uint64_t>(std::floor(
                difference / current_rate_ratio_));
        std::uint64_t maximum_discard{};
        if (!CheckedMultiply(
            config_.period_frames,
            kMaximumPrimingDiscardPeriods,
            &maximum_discard))
        {
            LatchFault(
                AsioPresentationBridgeFault::ArithmeticOverflow);
            return false;
        }
        discard_frames =
            (std::min)(discard_frames, maximum_discard);

        std::uint64_t required_output{};
        if (!CheckedAdd(
                discard_frames,
                config_.period_frames,
                &required_output) ||
            !EnsureInputForOutput(required_output))
        {
            return false;
        }

        if (discard_frames != 0)
        {
            const auto discarded =
                rate_matcher_->DiscardOutputFrames(
                    discard_frames);
            if (!discarded ||
                *discarded != discard_frames)
            {
                conversion_failures_.fetch_add(
                    1, std::memory_order_relaxed);
                LatchFault(
                    AsioPresentationBridgeFault::
                    ConversionFailure);
                return false;
            }
            source_phase_ +=
                current_rate_ratio_ *
                static_cast<double>(discard_frames);
        }

        if (std::abs(
                target_source_phase - source_phase_) >
            kPrimingAlignmentToleranceFrames)
        {
            return false;
        }
        return ProduceRunningPeriod(
            target_source_phase, output, true);
    }

    AsioPresentationProcessResult
    AsioPresentationBridge::Process(
        const AsioRenderRequest& request,
        const std::span<float> interleaved_stereo_output) noexcept
    {
        if (interleaved_stereo_output.size() !=
            static_cast<std::size_t>(config_.period_frames) *
            kChannels)
        {
            std::ranges::fill(interleaved_stereo_output, 0.0F);
            LatchFault(
                AsioPresentationBridgeFault::
                InvalidOutputBuffer);
            return {
                .state = state(),
                .first_fault =
                AsioPresentationBridgeFault::
                InvalidOutputBuffer,
                .output_frames = 0,
                .audible = false,
            };
        }
        std::ranges::fill(interleaved_stereo_output, 0.0F);

        if (!TryAcquireClaim())
        {
            if (state() ==
                AsioPresentationBridgeState::Running)
            {
                LatchFault(
                    AsioPresentationBridgeFault::
                    ConcurrentAccess);
            }
            return Result(false);
        }

        callbacks_.fetch_add(1, std::memory_order_relaxed);
        bool audible = false;
        const auto current = state();
        if (current != AsioPresentationBridgeState::Faulted &&
            current !=
            AsioPresentationBridgeState::Quiescing)
        {
            double target_source_phase{};
            if (ValidateAndProjectTarget(
                request, &target_source_phase))
            {
                const auto validated_state = state();
                if (validated_state ==
                    AsioPresentationBridgeState::Armed)
                {
                    audible = ProcessArmed(
                        target_source_phase,
                        interleaved_stereo_output);
                }
                else if (validated_state ==
                    AsioPresentationBridgeState::Running)
                {
                    audible = ProduceRunningPeriod(
                        target_source_phase,
                        interleaved_stereo_output,
                        false);
                }
            }
        }

        if (!audible &&
            (state() ==
                AsioPresentationBridgeState::Priming ||
                state() ==
                AsioPresentationBridgeState::Armed))
        {
            priming_callbacks_.fetch_add(
                1, std::memory_order_relaxed);
        }
        const auto result = Result(audible);
        ReleaseClaim();
        return result;
    }

    AsioPresentationProcessResult
    AsioPresentationBridge::Result(
        const bool audible) const noexcept
    {
        return {
            .state = state(),
            .first_fault =
            static_cast<AsioPresentationBridgeFault>(
                first_fault_.load(
                    std::memory_order_acquire)),
            .output_frames = config_.period_frames,
            .audible = audible,
        };
    }

    AsioPresentationBridgeSnapshot
    AsioPresentationBridge::Snapshot() const noexcept
    {
        return {
            .state = state(),
            .first_fault =
            static_cast<AsioPresentationBridgeFault>(
                first_fault_.load(
                    std::memory_order_acquire)),
            .physical_session_generation =
            config_.physical_session_generation,
            .callbacks =
            callbacks_.load(std::memory_order_acquire),
            .priming_callbacks =
            priming_callbacks_.load(
                std::memory_order_acquire),
            .running_callbacks =
            running_callbacks_.load(
                std::memory_order_acquire),
            .handoff_logical_tail =
            handoff_logical_tail_.load(
                std::memory_order_acquire),
            .logical_rendered_frames =
            logical_rendered_frames_.load(
                std::memory_order_acquire),
            .input_high_water_frames =
            input_high_water_frames_.load(
                std::memory_order_acquire),
            .input_underflows =
            input_underflows_.load(
                std::memory_order_acquire),
            .input_overflows =
            input_overflows_.load(
                std::memory_order_acquire),
            .conversion_failures =
            conversion_failures_.load(
                std::memory_order_acquire),
            .phase_envelope_violations =
            phase_envelope_violations_.load(
                std::memory_order_acquire),
            .non_finite_output_blocks =
            non_finite_output_blocks_.load(
                std::memory_order_acquire),
            .resampler_input_latency_frames =
            rate_matcher_->input_latency_frames(),
            .resampler_output_latency_frames =
            rate_matcher_->output_latency_frames(),
            .phase_envelope_frames = phase_envelope_frames_,
            .initial_phase_error_frames =
            LoadDouble(initial_phase_error_bits_),
            .maximum_absolute_phase_error_frames =
            LoadDouble(
                maximum_absolute_phase_error_bits_),
            .final_phase_error_frames =
            LoadDouble(final_phase_error_bits_),
            .minimum_rate_ratio_ppm =
            LoadDouble(minimum_rate_ratio_ppm_bits_),
            .maximum_rate_ratio_ppm =
            LoadDouble(maximum_rate_ratio_ppm_bits_),
            .final_rate_ratio_ppm =
            LoadDouble(final_rate_ratio_ppm_bits_),
        };
    }

    AsioPresentationBridgeState
    AsioPresentationBridge::state() const noexcept
    {
        return static_cast<AsioPresentationBridgeState>(
            state_.load(std::memory_order_acquire));
    }
} // namespace gc::audio
