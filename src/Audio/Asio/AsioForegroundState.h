#pragma once
// SPDX-License-Identifier: CC0-1.0

#include <atomic>
#include <cstdint>
#include <limits>

namespace gc::audio
{
    enum class AsioForegroundPublishResult : std::uint8_t
    {
        unchanged,
        changed,
        generation_overflow,
    };

    struct AsioForegroundSnapshot final
    {
        bool is_foreground{};
        std::uint64_t loss_generation{};
    };

    class AsioForegroundState final
    {
    public:
        [[nodiscard]] AsioForegroundPublishResult Publish(
            const bool foreground) noexcept
        {
            auto observed = encoded_.load(std::memory_order_relaxed);
            for (;;)
            {
                const bool previous_foreground =
                    (observed & kForegroundBit) != 0;
                if (previous_foreground == foreground)
                {
                    return AsioForegroundPublishResult::unchanged;
                }

                auto generation = observed >> 1;
                if (!foreground)
                {
                    if (generation == kMaximumGeneration)
                    {
                        return AsioForegroundPublishResult::generation_overflow;
                    }
                    ++generation;
                }
                const auto desired = (generation << 1) |
                    (foreground ? kForegroundBit : 0);
                if (encoded_.compare_exchange_weak(
                    observed,
                    desired,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    return AsioForegroundPublishResult::changed;
                }
            }
        }

        [[nodiscard]] AsioForegroundSnapshot Read() const noexcept
        {
            const auto encoded = encoded_.load(std::memory_order_acquire);
            return {
                .is_foreground = (encoded & kForegroundBit) != 0,
                .loss_generation = encoded >> 1,
            };
        }

    private:
        static_assert(std::atomic_uint64_t::is_always_lock_free);

        static constexpr std::uint64_t kForegroundBit = 1;
        static constexpr std::uint64_t kMaximumGeneration =
            (std::numeric_limits<std::uint64_t>::max)() >> 1;

        std::atomic_uint64_t encoded_{};
    };
} // namespace gc::audio
