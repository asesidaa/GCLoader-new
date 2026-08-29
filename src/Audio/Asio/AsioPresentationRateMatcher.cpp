#include "Audio/Asio/AsioPresentationRateMatcher.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr std::uint64_t kChannels = 2;
        constexpr std::uint64_t kInputPeriods = 12;
        constexpr std::uint32_t kRateRatioDenominator = 1'000'000;

        [[nodiscard]] bool CanAddressSamples(
            const std::uint64_t frames) noexcept
        {
            return frames <=
                (std::numeric_limits<std::size_t>::max)() /
                    kChannels;
        }
    } // namespace

    AsioPresentationRateMatcher::AsioPresentationRateMatcher(
        const std::uint32_t logical_rate,
        const std::uint32_t driver_rate,
        const std::uint32_t period_frames,
        std::shared_ptr<const ma_allocation_callbacks>
            allocation_callbacks) noexcept
        : logical_rate_(logical_rate),
          driver_rate_(driver_rate),
          period_frames_(period_frames),
          allocation_callbacks_(std::move(allocation_callbacks))
    {
    }

    AsioPresentationRateMatcher::~AsioPresentationRateMatcher()
    {
        if (initialized_)
        {
            ma_resampler_uninit(
                &resampler_, allocation_callbacks_.get());
        }
    }

    std::expected<std::unique_ptr<AsioPresentationRateMatcher>,
                  AsioPresentationRateMatcherFailure>
    AsioPresentationRateMatcher::Create(
        const std::uint32_t logical_rate,
        const std::uint32_t driver_rate,
        const std::uint32_t period_frames,
        std::shared_ptr<const ma_allocation_callbacks>
            allocation_callbacks) noexcept
    {
        if (logical_rate == 0 || driver_rate == 0 ||
            period_frames == 0)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    InvalidConfiguration);
        }

        auto matcher =
            std::unique_ptr<AsioPresentationRateMatcher>{
                new(std::nothrow) AsioPresentationRateMatcher(
                    logical_rate,
                    driver_rate,
                    period_frames,
                    std::move(allocation_callbacks))
            };
        if (!matcher)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    InitializationFailed);
        }
        const auto initialized = matcher->Initialize();
        if (!initialized)
        {
            return std::unexpected(initialized.error());
        }
        return matcher;
    }

    std::expected<void, AsioPresentationRateMatcherFailure>
    AsioPresentationRateMatcher::Initialize() noexcept
    {
        auto config = ma_resampler_config_init(
            ma_format_f32,
            static_cast<ma_uint32>(kChannels),
            logical_rate_,
            driver_rate_,
            ma_resample_algorithm_linear);
        config.linear.lpfOrder = 0;

        const auto result = ma_resampler_init(
            &config, allocation_callbacks_.get(), &resampler_);
        if (result != MA_SUCCESS)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    InitializationFailed);
        }
        initialized_ = true;
        input_latency_frames_ =
            ma_resampler_get_input_latency(&resampler_);
        output_latency_frames_ =
            ma_resampler_get_output_latency(&resampler_);

        if (period_frames_ >
                (std::numeric_limits<std::uint64_t>::max)() /
                    kInputPeriods)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    ArithmeticOverflow);
        }
        const auto base_capacity =
            kInputPeriods * period_frames_;
        if (input_latency_frames_ >
                ((std::numeric_limits<std::uint64_t>::max)() -
                    base_capacity - 2) /
                    2)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    ArithmeticOverflow);
        }
        input_capacity_frames_ =
            base_capacity + 2 * input_latency_frames_ + 2;
        if (!CanAddressSamples(input_capacity_frames_) ||
            !CanAddressSamples(period_frames_))
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    ArithmeticOverflow);
        }

        try
        {
            input_.resize(static_cast<std::size_t>(
                input_capacity_frames_ * kChannels));
            output_.resize(static_cast<std::size_t>(
                static_cast<std::uint64_t>(period_frames_) *
                kChannels));
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    InitializationFailed);
        }
        return {};
    }

    std::expected<void, AsioPresentationRateMatcherFailure>
    AsioPresentationRateMatcher::Reset() noexcept
    {
        if (!initialized_)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    InvalidConfiguration);
        }

        // The generic miniaudio set_rate_ratio wrapper truncates to a
        // denominator of 1,000. SetRateRatio uses the public set_rate API
        // with a million-frame denominator so sub-1,000-ppm correction is
        // representable.
        const auto nominal = SetRateRatio(1.0);
        if (!nominal ||
            ma_resampler_reset(&resampler_) != MA_SUCCESS)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    RateChangeFailed);
        }
        input_read_frame_ = 0;
        buffered_input_frames_ = 0;
        return {};
    }

    std::expected<double, AsioPresentationRateMatcherFailure>
    AsioPresentationRateMatcher::SetRateRatio(
        const double input_per_output_ratio) noexcept
    {
        if (!initialized_ ||
            !std::isfinite(input_per_output_ratio) ||
            input_per_output_ratio <= 0.0)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    InvalidRateRatio);
        }

        const double scaled =
            input_per_output_ratio *
            static_cast<double>(kRateRatioDenominator);
        if (!std::isfinite(scaled) || scaled < 1.0 ||
            scaled >
                static_cast<double>(
                    (std::numeric_limits<std::uint32_t>::max)()))
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    InvalidRateRatio);
        }
        const auto numerator =
            static_cast<std::uint32_t>(std::llround(scaled));
        if (numerator == 0 ||
            ma_resampler_set_rate(
                &resampler_,
                numerator,
                kRateRatioDenominator) != MA_SUCCESS)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    RateChangeFailed);
        }

        rate_ratio_ =
            static_cast<double>(numerator) /
            static_cast<double>(kRateRatioDenominator);
        return rate_ratio_;
    }

    std::expected<std::uint64_t,
                  AsioPresentationRateMatcherFailure>
    AsioPresentationRateMatcher::RequiredInputFrames(
        const std::uint64_t output_frames) const noexcept
    {
        if (!initialized_)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    InvalidConfiguration);
        }
        ma_uint64 required{};
        if (ma_resampler_get_required_input_frame_count(
                &resampler_,
                output_frames,
                &required) != MA_SUCCESS)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    ConversionFailed);
        }
        return required;
    }

    std::expected<void, AsioPresentationRateMatcherFailure>
    AsioPresentationRateMatcher::Push(
        const std::span<const float> interleaved_stereo) noexcept
    {
        if (!initialized_ ||
            interleaved_stereo.size() % kChannels != 0)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::
                    InvalidConfiguration);
        }
        const auto frames =
            static_cast<std::uint64_t>(
                interleaved_stereo.size() / kChannels);
        if (frames > free_input_frames())
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::InputOverflow);
        }

        const auto write_frame =
            (input_read_frame_ + buffered_input_frames_) %
            input_capacity_frames_;
        const auto first_frames = (std::min)(
            frames, input_capacity_frames_ - write_frame);
        const auto first_samples =
            static_cast<std::size_t>(first_frames * kChannels);
        std::copy_n(
            interleaved_stereo.begin(),
            first_samples,
            input_.begin() +
                static_cast<std::ptrdiff_t>(
                    write_frame * kChannels));
        const auto second_frames = frames - first_frames;
        if (second_frames != 0)
        {
            std::copy_n(
                interleaved_stereo.begin() +
                    static_cast<std::ptrdiff_t>(first_samples),
                static_cast<std::size_t>(
                    second_frames * kChannels),
                input_.begin());
        }
        buffered_input_frames_ += frames;
        return {};
    }

    std::expected<std::uint64_t,
                  AsioPresentationRateMatcherFailure>
    AsioPresentationRateMatcher::Process(
        float* output,
        const std::uint64_t output_frames) noexcept
    {
        const auto required = RequiredInputFrames(output_frames);
        if (!required)
        {
            return std::unexpected(required.error());
        }
        if (*required > buffered_input_frames_)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::InputUnderflow);
        }

        std::uint64_t produced = 0;
        for (std::uint32_t call = 0;
             call < 2 && produced < output_frames;
             ++call)
        {
            const auto contiguous = (std::min)(
                buffered_input_frames_,
                input_capacity_frames_ - input_read_frame_);
            ma_uint64 consumed = contiguous;
            ma_uint64 requested = output_frames - produced;
            const float* input = contiguous == 0
                ? nullptr
                : input_.data() +
                    static_cast<std::ptrdiff_t>(
                        input_read_frame_ * kChannels);
            float* destination = output == nullptr
                ? nullptr
                : output +
                    static_cast<std::ptrdiff_t>(
                        produced * kChannels);
            const auto result = ma_resampler_process_pcm_frames(
                &resampler_,
                input,
                &consumed,
                destination,
                &requested);
            if (result != MA_SUCCESS)
            {
                return std::unexpected(
                    AsioPresentationRateMatcherFailure::
                        ConversionFailed);
            }

            input_read_frame_ =
                (input_read_frame_ + consumed) %
                input_capacity_frames_;
            buffered_input_frames_ -= consumed;
            produced += requested;
            if (consumed == 0 && requested == 0)
            {
                break;
            }
        }

        if (produced != output_frames)
        {
            return std::unexpected(
                AsioPresentationRateMatcherFailure::InputUnderflow);
        }
        return produced;
    }

    std::expected<std::span<const float>,
                  AsioPresentationRateMatcherFailure>
    AsioPresentationRateMatcher::ProcessPeriod() noexcept
    {
        const auto processed =
            Process(output_.data(), period_frames_);
        if (!processed)
        {
            return std::unexpected(processed.error());
        }
        return std::span<const float>{output_};
    }

    std::expected<std::uint64_t,
                  AsioPresentationRateMatcherFailure>
    AsioPresentationRateMatcher::DiscardOutputFrames(
        const std::uint64_t output_frames) noexcept
    {
        if (output_frames == 0)
        {
            return std::uint64_t{0};
        }
        return Process(nullptr, output_frames);
    }

    std::uint64_t
    AsioPresentationRateMatcher::input_latency_frames() const noexcept
    {
        return input_latency_frames_;
    }

    std::uint64_t
    AsioPresentationRateMatcher::output_latency_frames() const noexcept
    {
        return output_latency_frames_;
    }

    std::uint64_t
    AsioPresentationRateMatcher::input_capacity_frames() const noexcept
    {
        return input_capacity_frames_;
    }

    std::uint64_t
    AsioPresentationRateMatcher::buffered_input_frames() const noexcept
    {
        return buffered_input_frames_;
    }

    std::uint64_t
    AsioPresentationRateMatcher::free_input_frames() const noexcept
    {
        return input_capacity_frames_ - buffered_input_frames_;
    }

    std::uint32_t
    AsioPresentationRateMatcher::period_frames() const noexcept
    {
        return period_frames_;
    }

    double
    AsioPresentationRateMatcher::rate_ratio() const noexcept
    {
        return rate_ratio_;
    }
} // namespace gc::audio
