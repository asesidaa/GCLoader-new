#include "Audio/Logical/LogicalPresentedOutputClock.h"

#include <utility>

namespace gc::audio
{
    LogicalPresentedOutputClock::LogicalPresentedOutputClock(
        const LogicalPresentedOutputClockActions actions,
        std::shared_ptr<const LogicalPresentationClock> timeline) noexcept
        : actions_(actions),
          timeline_(std::move(timeline))
    {
    }

    std::optional<std::uint64_t>
    LogicalPresentedOutputClock::CurrentOutputFrame() noexcept
    {
        if (invalidated_.load(std::memory_order_acquire) ||
            timeline_ == nullptr || actions_.time_get_time_ms == nullptr)
        {
            return std::nullopt;
        }

        const auto frame = timeline_->WholeFrameAt(
            actions_.time_get_time_ms(actions_.context));
        if (!frame || invalidated_.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }
        return RememberMonotonic(*frame);
    }

    void LogicalPresentedOutputClock::Invalidate() noexcept
    {
        invalidated_.store(true, std::memory_order_release);
    }

    std::uint64_t LogicalPresentedOutputClock::RememberMonotonic(
        const std::uint64_t frame) noexcept
    {
        auto previous = last_returned_.load(std::memory_order_relaxed);
        while (previous < frame &&
            !last_returned_.compare_exchange_weak(
                previous,
                frame,
                std::memory_order_release,
                std::memory_order_relaxed))
        {
        }
        return previous < frame ? frame : previous;
    }
} // namespace gc::audio
