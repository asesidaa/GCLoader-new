#include "Patches/Framerate/FramerateMenuTiming.h"

#include <array>
#include <format>

namespace gc::framerate
{
    namespace
    {
        template <typename... Values>
        constexpr BytePattern Pattern(Values... values) noexcept
        {
            static_assert(sizeof...(Values) <= kMaximumPatternBytes);
            BytePattern result{};
            result.size = static_cast<std::uint8_t>(sizeof...(Values));
            std::size_t index = 0;
            ((result.bytes[index++] =
                static_cast<std::byte>(static_cast<std::uint8_t>(values))), ...);
            return result;
        }

        constexpr std::array<MenuTimingHookSite, 6> kMenuTimingHookSites{
            {
                {
                    .contract = {
                        .id = FramerateHookId::MovieClipPreprocessVisit,
                        .rva = 0x000EFB90,
                        .expected =
                        Pattern(0x6A, 0xFF, 0x68, 0x10, 0x49, 0x67, 0x00),
                        .name = "MovieClip preprocessing visitor scope",
                    },
                    .kind = MenuTimingHookKind::Inline,
                },
                {
                    .contract = {
                        .id = FramerateHookId::RankingEntryCounterStore,
                        .rva = kRankingEntryCounterHookGeometry.hook_rva,
                        .expected = Pattern(0x8B, 0x4D, 0xE0, 0x89, 0x01),
                        .name = "Ranking entry authored counter store",
                    },
                    .kind = MenuTimingHookKind::Mid,
                },
                {
                    .contract = {
                        .id = FramerateHookId::HitChartEntryCounterStore,
                        .rva = kHitChartEntryCounterHookGeometry.hook_rva,
                        .expected = Pattern(
                            0x8B, 0x8D, 0x6C, 0xFF, 0xFF, 0xFF),
                        .name = "HitChart entry authored counter store",
                    },
                    .kind = MenuTimingHookKind::Mid,
                },
                {
                    .contract = {
                        .id = FramerateHookId::UnlockRewardCountdownStore,
                        .rva = kUnlockRewardCountdownHookGeometry.hook_rva,
                        .expected = Pattern(0x89, 0x90, 0x6C, 0x37, 0x00, 0x00),
                        .name = "UnlockReward countdown authored counter store",
                    },
                    .kind = MenuTimingHookKind::Mid,
                },
                {
                    .contract = {
                        .id = FramerateHookId::UnlockRewardPrimaryStateStore,
                        .rva = kUnlockRewardPrimaryHookGeometry.hook_rva,
                        .expected = Pattern(0x89, 0x81, 0xD4, 0x37, 0x00, 0x00),
                        .name = "UnlockReward primary-state authored counter store",
                    },
                    .kind = MenuTimingHookKind::Mid,
                },
                {
                    .contract = {
                        .id = FramerateHookId::UnlockRewardSecondaryStateStore,
                        .rva = kUnlockRewardSecondaryHookGeometry.hook_rva,
                        .expected = Pattern(0x89, 0x90, 0xD4, 0x37, 0x00, 0x00),
                        .name = "UnlockReward secondary-state authored counter store",
                    },
                    .kind = MenuTimingHookKind::Mid,
                },
            }
        };
    } // namespace

    std::span<const MenuTimingHookSite>
    FramerateMenuTimingHookSites() noexcept
    {
        return kMenuTimingHookSites;
    }

    MovieClipAdvanceDecision DecideMovieClipAdvance(
        MovieClipAdvanceContext context,
        bool authored_tick) noexcept
    {
        if (context == MovieClipAdvanceContext::Goto)
        {
            return {};
        }
        if (context == MovieClipAdvanceContext::Preprocess)
        {
            return {
                .action = MovieClipAdvanceAction::ExecuteOriginal,
                .preprocessing_forced = !authored_tick,
            };
        }
        return {
            .action = authored_tick
                          ? MovieClipAdvanceAction::ExecuteOriginal
                          : MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
        };
    }

    bool ShouldHoldUnlockRewardPromptFrame(
        MovieClipAdvanceContext context,
        std::uint32_t instance_name_hash,
        std::string_view instance_name,
        std::uint32_t owner_name_hash,
        std::string_view owner_name,
        std::uint64_t current_frame,
        std::uint32_t stopped) noexcept
    {
        if (context != MovieClipAdvanceContext::Ordinary ||
            owner_name_hash != kUnlockRewardNavigatorNameHash ||
            owner_name != "imc_un_navi" ||
            current_frame != 1 ||
            stopped != 0)
        {
            return false;
        }

        return
            (instance_name_hash ==
                kUnlockRewardPromptTransitionNameHash &&
                instance_name == "imc_tx") ||
            (instance_name_hash ==
                kUnlockRewardPromptStableNameHash &&
                instance_name == "igr_un_instmsg01_img");
    }

    MenuCounterStoreAction DecideMenuCounterStore(
        bool authored_tick) noexcept
    {
        return authored_tick
                   ? MenuCounterStoreAction::Commit
                   : MenuCounterStoreAction::Suppress;
    }

    MenuCounterStoreAction ApplyMenuCounterStoreGate(
        safetyhook::Context& context,
        bool authored_tick,
        std::uintptr_t suppress_resume_eip) noexcept
    {
        const auto action = DecideMenuCounterStore(authored_tick);
        if (action == MenuCounterStoreAction::Suppress)
        {
            context.eip =
                static_cast<std::uint32_t>(suppress_resume_eip);
        }
        return action;
    }

    void MovieClipPreprocessDepth::Enter() noexcept
    {
        ++depth_;
    }

    void MovieClipPreprocessDepth::Leave() noexcept
    {
        if (depth_ != 0)
        {
            --depth_;
        }
    }

    bool MovieClipPreprocessDepth::active() const noexcept
    {
        return depth_ != 0;
    }

    std::uint32_t MovieClipPreprocessDepth::depth() const noexcept
    {
        return depth_;
    }

    MovieClipPreprocessScope::MovieClipPreprocessScope(
        MovieClipPreprocessDepth& depth) noexcept
        : depth_(&depth)
    {
        depth_->Enter();
    }

    MovieClipPreprocessScope::~MovieClipPreprocessScope() noexcept
    {
        depth_->Leave();
    }

    std::string FormatFramerateMenuRuntimeStats(
        const FramerateMenuRuntimeStats& stats)
    {
        return std::format(
            " movieclip_preprocess={}/{} unlock_prompt_holds={}/{} "
            "ranking_entry={}/{} hitchart_entry={}/{} unlock_countdown={}/{} "
            "unlock_state_primary={}/{} unlock_state_secondary={}/{}",
            stats.preprocessing_visits,
            stats.preprocessing_forced,
            stats.unlock_prompt_transition_holds,
            stats.unlock_prompt_stable_holds,
            stats.ranking_entry.commits,
            stats.ranking_entry.suppressions,
            stats.hitchart_entry.commits,
            stats.hitchart_entry.suppressions,
            stats.unlock_countdown.commits,
            stats.unlock_countdown.suppressions,
            stats.unlock_primary.commits,
            stats.unlock_primary.suppressions,
            stats.unlock_secondary.commits,
            stats.unlock_secondary.suppressions);
    }
} // namespace gc::framerate
