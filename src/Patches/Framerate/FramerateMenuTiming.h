#pragma once

#include "Patches/Framerate/FrameratePatchPlan.h"

#include <safetyhook.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gc::framerate {

enum class MenuTimingHookKind {
    Inline,
    Mid,
};

struct MenuCounterHookGeometry {
    std::uintptr_t hook_rva{};
    std::uintptr_t suppress_resume_rva{};
};

inline constexpr MenuCounterHookGeometry
    kRankingEntryCounterHookGeometry{
        .hook_rva = 0x00216EB4,
        .suppress_resume_rva = 0x00216EB9,
    };
inline constexpr MenuCounterHookGeometry
    kHitChartEntryCounterHookGeometry{
        .hook_rva = 0x0026562F,
        .suppress_resume_rva = 0x00265637,
    };
inline constexpr MenuCounterHookGeometry
    kUnlockRewardCountdownHookGeometry{
        .hook_rva = 0x00030DA3,
        .suppress_resume_rva = 0x00030DA9,
    };
inline constexpr MenuCounterHookGeometry
    kUnlockRewardPrimaryHookGeometry{
        .hook_rva = 0x00030E54,
        .suppress_resume_rva = 0x00030E5A,
    };
inline constexpr MenuCounterHookGeometry
    kUnlockRewardSecondaryHookGeometry{
        .hook_rva = 0x00030F23,
        .suppress_resume_rva = 0x00030F29,
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

inline constexpr std::uint32_t
    kUnlockRewardPromptTransitionNameHash = 0xFCDA0604;
inline constexpr std::uint32_t
    kUnlockRewardPromptStableNameHash = 0x9D55AF65;
inline constexpr std::uint32_t
    kUnlockRewardNavigatorNameHash = 0x59FE24C8;

struct MovieClipAdvanceDecision {
    MovieClipAdvanceAction action{MovieClipAdvanceAction::ExecuteOriginal};
    bool preprocessing_non_tick_skip{};
    bool preprocessing_forced{};
};

[[nodiscard]] MovieClipAdvanceDecision DecideMovieClipAdvance(
    MenuTimingMode mode,
    MovieClipAdvanceContext context,
    bool authored_tick) noexcept;

[[nodiscard]] bool ShouldHoldUnlockRewardPromptFrame(
    MenuTimingMode mode,
    MovieClipAdvanceContext context,
    std::uint32_t instance_name_hash,
    std::string_view instance_name,
    std::uint32_t parent_name_hash,
    std::string_view parent_name) noexcept;

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
    std::uintptr_t suppress_resume_eip) noexcept;

using MenuDiagnosticReadU32 = bool (*)(
    std::uintptr_t address,
    std::uint32_t& value) noexcept;

[[nodiscard]] std::optional<std::uintptr_t>
ResolveMenuCounterDestinationFromFrame(
    const safetyhook::Context& context,
    std::intptr_t frame_offset,
    MenuDiagnosticReadU32 read_u32) noexcept;

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
    std::uint64_t unlock_prompt_transition_holds{};
    std::uint64_t unlock_prompt_stable_holds{};
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
