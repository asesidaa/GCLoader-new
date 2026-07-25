#include "Patches/Framerate/FramerateMenuTiming.h"

#include <sstream>

namespace gc::framerate {

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
    std::uint32_t instruction_length) noexcept {
    const auto action = DecideMenuCounterStore(mode, authored_tick);
    if (action == MenuCounterStoreAction::Suppress) {
        context.eip += instruction_length;
    }
    return action;
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
