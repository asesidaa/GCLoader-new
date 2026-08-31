#include "Audio/ExactJudgementTimeline.h"

#include <atomic>
#include <limits>
#include <mutex>

namespace gc::audio
{
    namespace
    {
        constexpr std::memory_order kExactClockAtomicOrder =
            std::memory_order_seq_cst;

        struct ActiveExactJudgementTimelineRegistry final
        {
            std::mutex mutex;
            std::weak_ptr<ExactJudgementTimeline> provider;
            std::uint64_t timeline_generation{};
        };

        ActiveExactJudgementTimelineRegistry g_active_provider;
        std::atomic<std::uint64_t> g_next_timeline_generation{1};
    } // namespace

    std::string_view ExactJudgementTimelineDomainName(
        ExactJudgementTimelineDomain domain) noexcept
    {
        switch (domain)
        {
        case ExactJudgementTimelineDomain::WasapiQpc:
            return "wasapi_qpc";
        default:
            return "invalid";
        }
    }

    std::shared_ptr<const ExactJudgementTimeline>
    AcquireExactJudgementTimeline() noexcept
    {
        std::lock_guard lock(g_active_provider.mutex);
        auto provider = g_active_provider.provider.lock();
        if (provider == nullptr ||
            provider->info().timeline_generation !=
            g_active_provider.timeline_generation)
        {
            return nullptr;
        }
        return provider;
    }

    namespace detail
    {
        std::uint64_t NextExactJudgementTimelineGeneration() noexcept
        {
            auto generation =
                g_next_timeline_generation.load(kExactClockAtomicOrder);
            for (;;)
            {
                if (generation == 0 ||
                    generation == std::numeric_limits<std::uint64_t>::max())
                {
                    return 0;
                }
                if (g_next_timeline_generation.compare_exchange_weak(
                    generation,
                    generation + 1,
                    kExactClockAtomicOrder,
                    kExactClockAtomicOrder))
                {
                    return generation;
                }
            }
        }

        bool RegisterExactJudgementTimeline(
            const std::shared_ptr<ExactJudgementTimeline>& provider) noexcept
        {
            if (provider == nullptr)
            {
                return false;
            }
            const auto generation = provider->info().timeline_generation;
            if (generation == 0)
            {
                return false;
            }

            std::lock_guard lock(g_active_provider.mutex);
            auto previous = g_active_provider.provider.lock();
            if (g_active_provider.timeline_generation == generation)
            {
                return previous == provider;
            }
            if (previous != nullptr)
            {
                previous->Invalidate();
            }
            g_active_provider.provider = provider;
            g_active_provider.timeline_generation = generation;
            return true;
        }

        void UnregisterExactJudgementTimeline(
            std::uint64_t expected_generation) noexcept
        {
            std::lock_guard lock(g_active_provider.mutex);
            if (g_active_provider.timeline_generation != expected_generation)
            {
                return;
            }
            g_active_provider.provider.reset();
            g_active_provider.timeline_generation = 0;
        }
    } // namespace detail
} // namespace gc::audio
