#pragma once

#include <miniaudio.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <vector>

namespace gc::audio
{
    enum class AsioPresentationRateMatcherFailure : std::uint8_t
    {
        InvalidConfiguration,
        InitializationFailed,
        ArithmeticOverflow,
        InvalidRateRatio,
        RateChangeFailed,
        InputOverflow,
        InputUnderflow,
        ConversionFailed,
    };

    class AsioPresentationRateMatcher final
    {
    public:
        [[nodiscard]] static
        std::expected<std::unique_ptr<AsioPresentationRateMatcher>,
                      AsioPresentationRateMatcherFailure>
        Create(
            std::uint32_t logical_rate,
            std::uint32_t driver_rate,
            std::uint32_t period_frames,
            std::shared_ptr<const ma_allocation_callbacks>
            allocation_callbacks = {}) noexcept;

        ~AsioPresentationRateMatcher();
        AsioPresentationRateMatcher(
            const AsioPresentationRateMatcher&) = delete;
        AsioPresentationRateMatcher& operator=(
            const AsioPresentationRateMatcher&) = delete;

        [[nodiscard]]
        std::expected<void, AsioPresentationRateMatcherFailure>
        Reset() noexcept;
        [[nodiscard]]
        std::expected<double, AsioPresentationRateMatcherFailure>
        SetRateRatio(double input_per_output_ratio) noexcept;
        [[nodiscard]]
        std::expected<std::uint64_t,
                      AsioPresentationRateMatcherFailure>
        RequiredInputFrames(
            std::uint64_t output_frames) const noexcept;
        [[nodiscard]]
        std::expected<void, AsioPresentationRateMatcherFailure>
        Push(std::span<const float> interleaved_stereo) noexcept;
        [[nodiscard]]
        std::expected<std::span<const float>,
                      AsioPresentationRateMatcherFailure>
        ProcessPeriod() noexcept;
        [[nodiscard]]
        std::expected<std::uint64_t,
                      AsioPresentationRateMatcherFailure>
        DiscardOutputFrames(std::uint64_t output_frames) noexcept;

        [[nodiscard]] std::uint64_t input_latency_frames() const noexcept;
        [[nodiscard]] std::uint64_t output_latency_frames() const noexcept;
        [[nodiscard]] std::uint64_t input_capacity_frames() const noexcept;
        [[nodiscard]] std::uint64_t buffered_input_frames() const noexcept;
        [[nodiscard]] std::uint64_t free_input_frames() const noexcept;
        [[nodiscard]] std::uint32_t period_frames() const noexcept;
        [[nodiscard]] double rate_ratio() const noexcept;

    private:
        AsioPresentationRateMatcher(
            std::uint32_t logical_rate,
            std::uint32_t driver_rate,
            std::uint32_t period_frames,
            std::shared_ptr<const ma_allocation_callbacks>
            allocation_callbacks) noexcept;

        [[nodiscard]]
        std::expected<void, AsioPresentationRateMatcherFailure>
        Initialize() noexcept;
        [[nodiscard]]
        std::expected<std::uint64_t,
                      AsioPresentationRateMatcherFailure>
        Process(
            float* output,
            std::uint64_t output_frames) noexcept;

        const std::uint32_t logical_rate_;
        const std::uint32_t driver_rate_;
        const std::uint32_t period_frames_;
        std::shared_ptr<const ma_allocation_callbacks>
        allocation_callbacks_;
        ma_resampler resampler_{};
        bool initialized_{};

        std::vector<float> input_;
        std::vector<float> output_;
        std::uint64_t input_capacity_frames_{};
        std::uint64_t input_read_frame_{};
        std::uint64_t buffered_input_frames_{};
        std::uint64_t input_latency_frames_{};
        std::uint64_t output_latency_frames_{};
        double rate_ratio_{1.0};
    };
} // namespace gc::audio
