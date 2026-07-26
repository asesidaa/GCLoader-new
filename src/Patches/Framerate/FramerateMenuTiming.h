#pragma once

#include "Patches/Framerate/FrameratePatchPlan.h"

#include <safetyhook.hpp>

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

// game471.exe MovieClipInstance layout, proved from the constructor,
// placement core, Stop, and AdvanceOneTimelineFrame.
inline constexpr std::uintptr_t kMovieClipStopFlagOffset = 0x11C;
inline constexpr std::uintptr_t kMovieClipInstanceNameOffset = 0x120;
inline constexpr std::uintptr_t kMovieClipInstanceNameHashOffset = 0x140;
inline constexpr std::uintptr_t kMovieClipOwnerOffset = 0x150;
inline constexpr std::uintptr_t kMovieClipCurrentFrameLowOffset = 0x178;
inline constexpr std::uintptr_t kMovieClipCurrentFrameHighOffset = 0x17C;

struct MovieClipAdvanceDecision {
    MovieClipAdvanceAction action{MovieClipAdvanceAction::ExecuteOriginal};
    bool preprocessing_forced{};
};

[[nodiscard]] MovieClipAdvanceDecision DecideMovieClipAdvance(
    MovieClipAdvanceContext context,
    bool authored_tick) noexcept;

[[nodiscard]] bool ShouldHoldUnlockRewardPromptFrame(
    MovieClipAdvanceContext context,
    std::uint32_t instance_name_hash,
    std::string_view instance_name,
    std::uint32_t owner_name_hash,
    std::string_view owner_name,
    std::uint64_t current_frame,
    std::uint32_t stopped) noexcept;

enum class MenuCounterStoreAction {
    Commit,
    Suppress,
};

[[nodiscard]] MenuCounterStoreAction DecideMenuCounterStore(
    bool authored_tick) noexcept;

[[nodiscard]] MenuCounterStoreAction ApplyMenuCounterStoreGate(
    safetyhook::Context& context,
    bool authored_tick,
    std::uintptr_t suppress_resume_eip) noexcept;

class MovieClipPreprocessDepth {
public:
    void Enter() noexcept;
    void Leave() noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::uint32_t depth() const noexcept;

private:
    std::uint32_t depth_{};
};

class MovieClipPreprocessScope {
public:
    explicit MovieClipPreprocessScope(
        MovieClipPreprocessDepth& depth) noexcept;
    ~MovieClipPreprocessScope() noexcept;

    MovieClipPreprocessScope(const MovieClipPreprocessScope&) = delete;
    MovieClipPreprocessScope& operator=(
        const MovieClipPreprocessScope&) = delete;

private:
    MovieClipPreprocessDepth* depth_{};
};

struct MenuCounterPathStats {
    std::uint64_t commits{};
    std::uint64_t suppressions{};
};

struct FramerateMenuRuntimeStats {
    std::uint64_t preprocessing_visits{};
    std::uint64_t preprocessing_forced{};
    std::uint64_t unlock_prompt_transition_holds{};
    std::uint64_t unlock_prompt_stable_holds{};
    MenuCounterPathStats ranking_entry{};
    MenuCounterPathStats hitchart_entry{};
    MenuCounterPathStats unlock_countdown{};
    MenuCounterPathStats unlock_primary{};
    MenuCounterPathStats unlock_secondary{};
};

[[nodiscard]] std::string FormatFramerateMenuRuntimeStats(
    const FramerateMenuRuntimeStats& stats);

} // namespace gc::framerate
