#include "Patches/Framerate/MenuTimingHooks.h"
#include "Patches/Framerate/FramerateTimingRuntime.h"
#include "Patches/Framerate/FramerateMenuTiming.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace gc::framerate::detail {
MenuTimingOriginals g_menu_originals;
thread_local MovieClipPreprocessDepth g_movieclip_preprocess_depth;
namespace {
constexpr std::size_t kMaximumMovieClipInstanceNameBytes = 32;
}
[[nodiscard]] bool ReadCStringSafe(
    std::uintptr_t address,
    std::span<char> destination,
    std::size_t& length) noexcept
{
    length = 0;
    if (address == 0 || destination.empty())
    {
        return false;
    }

    __try
    {
        for (std::size_t index = 0;
             index < destination.size();
             ++index)
        {
            const char value =
                *reinterpret_cast<const volatile char*>(
                    address + index);
            if (value == '\0')
            {
                length = index;
                return true;
            }
            destination[index] = value;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] std::optional<UnlockRewardPromptTarget>
IdentifyUnlockRewardPromptHold(
    void* self,
    MovieClipAdvanceContext context) noexcept
{
    if (!g_runtime->layout.unlock_reward_prompt_available ||
        context != MovieClipAdvanceContext::Ordinary)
    {
        return std::nullopt;
    }

    const auto movieclip = reinterpret_cast<std::uintptr_t>(self);
    std::uint32_t instance_name_hash{};
    if (!ReadU32Safe(
        movieclip + g_runtime->layout.movieclip_instance_hash,
        instance_name_hash))
    {
        return std::nullopt;
    }

    UnlockRewardPromptTarget target{};
    if (instance_name_hash ==
        kUnlockRewardPromptTransitionNameHash)
    {
        target = UnlockRewardPromptTarget::Transition;
    }
    else if (instance_name_hash ==
        kUnlockRewardPromptStableNameHash)
    {
        target = UnlockRewardPromptTarget::Stable;
    }
    else
    {
        return std::nullopt;
    }

    std::uint32_t instance_name_address{};
    std::uint32_t owner{};
    if (!ReadU32Safe(
            movieclip + g_runtime->layout.movieclip_instance_name,
            instance_name_address) ||
        instance_name_address == 0 ||
        !ReadU32Safe(
            movieclip + g_runtime->layout.movieclip_owner,
            owner) ||
        owner == 0)
    {
        return std::nullopt;
    }

    std::array<
        char,
        kMaximumMovieClipInstanceNameBytes> instance_name{};
    std::size_t instance_name_length{};
    if (!ReadCStringSafe(
        instance_name_address,
        instance_name,
        instance_name_length))
    {
        return std::nullopt;
    }

    const auto owner_address =
        static_cast<std::uintptr_t>(owner);
    std::uint32_t owner_name_hash{};
    std::uint32_t owner_name_address{};
    if (!ReadU32Safe(
            owner_address + g_runtime->layout.movieclip_instance_hash,
            owner_name_hash) ||
        !ReadU32Safe(
            owner_address + g_runtime->layout.movieclip_instance_name,
            owner_name_address) ||
        owner_name_address == 0)
    {
        return std::nullopt;
    }

    std::array<
        char,
        kMaximumMovieClipInstanceNameBytes> owner_name{};
    std::size_t owner_name_length{};
    if (!ReadCStringSafe(
        owner_name_address,
        owner_name,
        owner_name_length))
    {
        return std::nullopt;
    }

    std::uint32_t frame_low{};
    std::uint32_t frame_high{};
    std::uint32_t stopped{};
    if (!ReadU32Safe(
            movieclip + g_runtime->layout.movieclip_frame_low,
            frame_low) ||
        !ReadU32Safe(
            movieclip + g_runtime->layout.movieclip_frame_high,
            frame_high) ||
        !ReadU32Safe(
            movieclip + g_runtime->layout.movieclip_stop_flag,
            stopped))
    {
        return std::nullopt;
    }
    const std::uint64_t current_frame =
        (static_cast<std::uint64_t>(frame_high) << 32U) |
        frame_low;

    if (!ShouldHoldUnlockRewardPromptFrame(
        context,
        instance_name_hash,
        std::string_view{
            instance_name.data(),
            instance_name_length
        },
        owner_name_hash,
        std::string_view{
            owner_name.data(),
            owner_name_length
        },
        current_frame,
        stopped))
    {
        return std::nullopt;
    }
    return target;
}

void __fastcall HookMovieClipPreprocessVisit(
    void* self,
    void*,
    int traversal_arg)
{
    MovieClipPreprocessScope scope{g_movieclip_preprocess_depth};
    g_runtime->menu_counters.preprocessing_visits.fetch_add(
        1, std::memory_order_relaxed);
    g_menu_originals.movieclip_preprocess_visit(self, traversal_arg);
}

void ApplyPermanentMenuCounterStore(
    safetyhook::Context& context,
    MenuCounterRuntimeCounters& counters,
    std::uintptr_t suppress_resume_address) noexcept
{
    const auto action = ApplyMenuCounterStoreGate(
        context,
        IsAuthored60HzTick(),
        suppress_resume_address);
    auto& counter =
        action == MenuCounterStoreAction::Commit
            ? counters.commits
            : counters.suppressions;
    counter.fetch_add(1, std::memory_order_relaxed);
}

void HookRankingEntryCounterStore(safetyhook::Context& context)
{
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.ranking_entry,
        NativeTarget(FramerateNativeTarget::ranking_resume));
}

void HookHitChartEntryCounterStore(safetyhook::Context& context)
{
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.hitchart_entry,
        NativeTarget(FramerateNativeTarget::hitchart_resume));
}

void HookUnlockRewardCountdownStore(safetyhook::Context& context)
{
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.unlock_countdown,
        NativeTarget(FramerateNativeTarget::unlock_countdown_resume));
}

void HookUnlockRewardPrimaryStateStore(
    safetyhook::Context& context)
{
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.unlock_primary,
        NativeTarget(FramerateNativeTarget::unlock_primary_resume));
}

void HookUnlockRewardSecondaryStateStore(
    safetyhook::Context& context)
{
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.unlock_secondary,
        NativeTarget(FramerateNativeTarget::unlock_secondary_resume));
}

}
