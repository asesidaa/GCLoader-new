#pragma once

#include "Patches/Framerate/FrameratePatchPlan.h"

#include <safetyhook.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gc::framerate {

enum class MenuTimingHookKind {
    Inline,
    Mid,
};

struct MenuTimingHookSite {
    FramerateHookContract contract{};
    MenuTimingHookKind kind{};
};

[[nodiscard]] std::span<const MenuTimingHookSite>
FramerateMenuTimingHookSites() noexcept;

enum class MenuTimingMode {
    Observe,
    Correct,
};

[[nodiscard]] MenuTimingMode ActiveMenuTimingMode() noexcept;
[[nodiscard]] std::string_view MenuTimingModeName(
    MenuTimingMode mode) noexcept;

enum class MovieClipAdvanceContext {
    Ordinary,
    Goto,
    Preprocess,
};

enum class MovieClipAdvanceAction {
    ExecuteOriginal,
    ReturnSuccessWithoutMotion,
};

struct MovieClipAdvanceDecision {
    MovieClipAdvanceAction action{MovieClipAdvanceAction::ExecuteOriginal};
    bool preprocessing_non_tick_skip{};
    bool preprocessing_forced{};
};

[[nodiscard]] MovieClipAdvanceDecision DecideMovieClipAdvance(
    MenuTimingMode mode,
    MovieClipAdvanceContext context,
    bool authored_tick) noexcept;

enum class MenuCounterStoreAction {
    Commit,
    WouldSuppress,
    Suppress,
};

[[nodiscard]] MenuCounterStoreAction DecideMenuCounterStore(
    MenuTimingMode mode,
    bool authored_tick) noexcept;

[[nodiscard]] MenuCounterStoreAction ApplyMenuCounterStoreGate(
    safetyhook::Context& context,
    MenuTimingMode mode,
    bool authored_tick,
    std::uint32_t instruction_length) noexcept;

enum class PreprocessStopObservation {
    OutsidePreprocess,
    InPreprocess,
    CausalAfterSkippedAdvance,
};

class MovieClipPreprocessTracker {
public:
    static constexpr std::size_t kMaximumTrackedDepth = 32;

    void Enter(
        std::uintptr_t movieclip,
        std::uint64_t outer_epoch) noexcept;
    void Leave() noexcept;
    void RecordSkippedAdvance(
        std::uintptr_t movieclip,
        std::uint64_t outer_epoch) noexcept;
    [[nodiscard]] PreprocessStopObservation ObserveStop(
        std::uintptr_t movieclip,
        std::uint64_t outer_epoch) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;

private:
    struct Frame {
        std::uintptr_t movieclip{};
        std::uint64_t outer_epoch{};
        bool skipped_advance{};
    };

    std::array<Frame, kMaximumTrackedDepth> frames_{};
    std::size_t depth_{};
};

class MovieClipPreprocessScope {
public:
    MovieClipPreprocessScope(
        MovieClipPreprocessTracker& tracker,
        std::uintptr_t movieclip,
        std::uint64_t outer_epoch) noexcept;
    ~MovieClipPreprocessScope() noexcept;

    MovieClipPreprocessScope(const MovieClipPreprocessScope&) = delete;
    MovieClipPreprocessScope& operator=(
        const MovieClipPreprocessScope&) = delete;

private:
    MovieClipPreprocessTracker* tracker_{};
};

struct MovieClipVisitObservation {
    bool same_epoch_revisit{};
    bool hash_collision{};

    friend bool operator==(
        const MovieClipVisitObservation&,
        const MovieClipVisitObservation&) = default;
};

class MovieClipVisitTracker {
public:
    static constexpr std::size_t kSlotCount = 1024;

    [[nodiscard]] MovieClipVisitObservation Observe(
        std::uintptr_t movieclip,
        std::uint64_t outer_epoch) noexcept;

private:
    struct Slot {
        std::uintptr_t movieclip{};
        std::uint64_t outer_epoch{};
    };

    std::array<Slot, kSlotCount> slots_{};
};

struct MenuCounterPathStats {
    std::uint64_t commits{};
    std::uint64_t suppressions{};
};

struct MenuCounterBoundaryPathStats {
    std::uint64_t commits{};
    std::uint64_t suppressions{};
    std::uint64_t boundaries{};
};

struct FramerateMenuRuntimeStats {
    std::uint64_t preprocessing_visits{};
    std::uint64_t preprocessing_non_tick_skips{};
    std::uint64_t preprocessing_forced{};
    std::uint64_t preprocessing_stops{};
    std::uint64_t preprocessing_causal_stops{};
    std::uint64_t movieclip_same_epoch_revisits{};
    std::uint64_t movieclip_hash_collisions{};
    MenuCounterPathStats ranking_entry{};
    MenuCounterPathStats hitchart_entry{};
    MenuCounterBoundaryPathStats unlock_countdown{};
    MenuCounterBoundaryPathStats unlock_primary{};
    MenuCounterBoundaryPathStats unlock_secondary{};
    std::uint64_t diagnostic_read_failures{};
};

[[nodiscard]] std::string FormatFramerateMenuRuntimeStats(
    MenuTimingMode mode,
    const FramerateMenuRuntimeStats& stats);

} // namespace gc::framerate
