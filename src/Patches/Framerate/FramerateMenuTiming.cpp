#include "Patches/Framerate/FramerateMenuTiming.h"

#include <sstream>

namespace gc::framerate {

namespace {

template <typename... Values>
constexpr BytePattern Pattern(Values... values) noexcept {
    static_assert(sizeof...(Values) <= kMaximumPatternBytes);
    BytePattern result{};
    result.size = static_cast<std::uint8_t>(sizeof...(Values));
    std::size_t index = 0;
    ((result.bytes[index++] =
          static_cast<std::byte>(static_cast<std::uint8_t>(values))), ...);
    return result;
}

constexpr std::array<MenuTimingHookSite, 7> kMenuTimingHookSites{{
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
            .id = FramerateHookId::MovieClipStopDiagnostic,
            .rva = 0x000D1730,
            .expected = Pattern(
                0xC7, 0x81, 0x1C, 0x01, 0x00, 0x00,
                0x01, 0x00, 0x00, 0x00, 0xC3),
            .name = "MovieClip preprocessing stop diagnostic",
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
}};

} // namespace

std::span<const MenuTimingHookSite>
FramerateMenuTimingHookSites() noexcept {
    return kMenuTimingHookSites;
}

MenuTimingMode ActiveMenuTimingMode() noexcept {
    return MenuTimingMode::Observe;
}

std::string_view MenuTimingModeName(MenuTimingMode mode) noexcept {
    switch (mode) {
    case MenuTimingMode::Observe:
        return "observe";
    case MenuTimingMode::Correct:
        return "correct";
    }
    return "invalid";
}

MovieClipAdvanceDecision DecideMovieClipAdvance(
    MenuTimingMode mode,
    MovieClipAdvanceContext context,
    bool authored_tick) noexcept {
    if (context == MovieClipAdvanceContext::Goto) {
        return {};
    }
    if (context == MovieClipAdvanceContext::Preprocess) {
        if (authored_tick) {
            return {};
        }
        if (mode == MenuTimingMode::Correct) {
            return {
                .action = MovieClipAdvanceAction::ExecuteOriginal,
                .preprocessing_forced = true,
            };
        }
        return {
            .action =
                MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
            .preprocessing_non_tick_skip = true,
        };
    }
    return {
        .action = authored_tick
            ? MovieClipAdvanceAction::ExecuteOriginal
            : MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
    };
}

MenuCounterStoreAction DecideMenuCounterStore(
    MenuTimingMode mode,
    bool authored_tick) noexcept {
    if (authored_tick) {
        return MenuCounterStoreAction::Commit;
    }
    return mode == MenuTimingMode::Observe
        ? MenuCounterStoreAction::WouldSuppress
        : MenuCounterStoreAction::Suppress;
}

MenuCounterStoreAction ApplyMenuCounterStoreGate(
    safetyhook::Context& context,
    MenuTimingMode mode,
    bool authored_tick,
    std::uintptr_t suppress_resume_eip) noexcept {
    const auto action = DecideMenuCounterStore(mode, authored_tick);
    if (action == MenuCounterStoreAction::Suppress) {
        context.eip =
            static_cast<std::uint32_t>(suppress_resume_eip);
    }
    return action;
}

std::optional<std::uintptr_t>
ResolveMenuCounterDestinationFromFrame(
    const safetyhook::Context& context,
    std::intptr_t frame_offset,
    MenuDiagnosticReadU32 read_u32) noexcept {
    if (read_u32 == nullptr) {
        return std::nullopt;
    }

    std::uint32_t destination{};
    const auto slot_address =
        static_cast<std::uintptr_t>(context.ebp) +
        static_cast<std::uintptr_t>(frame_offset);
    if (!read_u32(slot_address, destination) ||
        destination == 0) {
        return std::nullopt;
    }
    return static_cast<std::uintptr_t>(destination);
}

void MovieClipPreprocessTracker::Enter(
    std::uintptr_t movieclip,
    std::uint64_t outer_epoch) noexcept {
    ++depth_;
    if (depth_ <= kMaximumTrackedDepth) {
        frames_[depth_ - 1] = {
            .movieclip = movieclip,
            .outer_epoch = outer_epoch,
        };
    }
}

void MovieClipPreprocessTracker::Leave() noexcept {
    if (depth_ == 0) {
        return;
    }
    if (depth_ <= kMaximumTrackedDepth) {
        frames_[depth_ - 1] = {};
    }
    --depth_;
}

void MovieClipPreprocessTracker::RecordSkippedAdvance(
    std::uintptr_t movieclip,
    std::uint64_t outer_epoch) noexcept {
    if (depth_ == 0 || depth_ > kMaximumTrackedDepth) {
        return;
    }
    for (std::size_t index = depth_; index > 0; --index) {
        auto& frame = frames_[index - 1];
        if (frame.outer_epoch != outer_epoch ||
            (frame.movieclip != 0 && frame.movieclip != movieclip)) {
            continue;
        }
        if (frame.movieclip == 0) {
            frame.movieclip = movieclip;
        }
        frame.skipped_advance = true;
        return;
    }
}

PreprocessStopObservation MovieClipPreprocessTracker::ObserveStop(
    std::uintptr_t movieclip,
    std::uint64_t outer_epoch) noexcept {
    if (depth_ == 0) {
        return PreprocessStopObservation::OutsidePreprocess;
    }
    if (depth_ > kMaximumTrackedDepth) {
        return PreprocessStopObservation::InPreprocess;
    }
    for (std::size_t index = depth_; index > 0; --index) {
        auto& frame = frames_[index - 1];
        if (frame.movieclip == movieclip &&
            frame.outer_epoch == outer_epoch &&
            frame.skipped_advance) {
            frame.skipped_advance = false;
            return PreprocessStopObservation::CausalAfterSkippedAdvance;
        }
    }
    return PreprocessStopObservation::InPreprocess;
}

bool MovieClipPreprocessTracker::active() const noexcept {
    return depth_ != 0;
}

std::size_t MovieClipPreprocessTracker::depth() const noexcept {
    return depth_;
}

MovieClipPreprocessScope::MovieClipPreprocessScope(
    MovieClipPreprocessTracker& tracker,
    std::uintptr_t movieclip,
    std::uint64_t outer_epoch) noexcept
    : tracker_(&tracker) {
    tracker_->Enter(movieclip, outer_epoch);
}

MovieClipPreprocessScope::~MovieClipPreprocessScope() noexcept {
    tracker_->Leave();
}

MovieClipVisitObservation MovieClipVisitTracker::Observe(
    std::uintptr_t movieclip,
    std::uint64_t outer_epoch) noexcept {
    const std::size_t index =
        (movieclip >> 4U) & (kSlotCount - 1U);
    auto& slot = slots_[index];
    const MovieClipVisitObservation observation{
        .same_epoch_revisit =
            slot.outer_epoch == outer_epoch &&
            slot.movieclip == movieclip &&
            movieclip != 0,
        .hash_collision =
            slot.outer_epoch == outer_epoch &&
            slot.movieclip != 0 &&
            slot.movieclip != movieclip,
    };
    slot = {
        .movieclip = movieclip,
        .outer_epoch = outer_epoch,
    };
    return observation;
}

std::string FormatFramerateMenuRuntimeStats(
    MenuTimingMode mode,
    const FramerateMenuRuntimeStats& stats) {
    std::ostringstream stream;
    stream
        << " menu_timing_mode=" << MenuTimingModeName(mode)
        << " movieclip_preprocess="
        << stats.preprocessing_visits << '/'
        << stats.preprocessing_non_tick_skips << '/'
        << stats.preprocessing_forced
        << " movieclip_preprocess_stop="
        << stats.preprocessing_stops << '/'
        << stats.preprocessing_causal_stops
        << " movieclip_revisit="
        << stats.movieclip_same_epoch_revisits << '/'
        << stats.movieclip_hash_collisions
        << " ranking_entry="
        << stats.ranking_entry.commits << '/'
        << stats.ranking_entry.suppressions
        << " hitchart_entry="
        << stats.hitchart_entry.commits << '/'
        << stats.hitchart_entry.suppressions
        << " unlock_countdown="
        << stats.unlock_countdown.commits << '/'
        << stats.unlock_countdown.suppressions << '/'
        << stats.unlock_countdown.boundaries
        << " unlock_state_primary="
        << stats.unlock_primary.commits << '/'
        << stats.unlock_primary.suppressions << '/'
        << stats.unlock_primary.boundaries
        << " unlock_state_secondary="
        << stats.unlock_secondary.commits << '/'
        << stats.unlock_secondary.suppressions << '/'
        << stats.unlock_secondary.boundaries
        << " menu_diagnostic_read_failures="
        << stats.diagnostic_read_failures;
    return stream.str();
}

} // namespace gc::framerate
