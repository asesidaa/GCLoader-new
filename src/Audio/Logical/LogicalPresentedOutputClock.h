#pragma once

#include "Audio/Logical/LogicalPresentationClock.h"
#include "Audio/Mixer/PresentedOutputClock.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

namespace gc::audio
{
    struct LogicalPresentedOutputClockActions final
    {
        void* context{};
        std::uint32_t (*time_get_time_ms)(void*) noexcept{};
    };

    class LogicalPresentedOutputClock final : public IPresentedOutputClock
    {
    public:
        LogicalPresentedOutputClock(
            LogicalPresentedOutputClockActions actions,
            std::shared_ptr<const LogicalPresentationClock> timeline) noexcept;

        [[nodiscard]] std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
        void Invalidate() noexcept override;

    private:
        [[nodiscard]] std::uint64_t
        RememberMonotonic(std::uint64_t frame) noexcept;

        LogicalPresentedOutputClockActions actions_{};
        std::shared_ptr<const LogicalPresentationClock> timeline_;
        std::atomic_bool invalidated_{};
        std::atomic_uint64_t last_returned_{};
    };
} // namespace gc::audio
