#pragma once

#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Patches/Framerate/GameplaySongClock.h"

#include <cstdint>
#include <optional>

namespace gc::framerate {

enum class FramerateHookId;
enum class GameplayAudioClockPlan : std::uint8_t;

[[nodiscard]] bool FramerateHookHasRuntimeBinding(
    FramerateHookId id) noexcept;
[[nodiscard]] bool FrameratePatchInit(
    bool authoritative_audio_clock_available);

namespace detail {

enum class GameplaySongClockInputState : std::uint8_t {
    Exact,
    Rounded,
    Inactive,
    Failed,
};

enum class GameplayCadenceTestRegister : std::uint8_t {
    Eax,
    Ecx,
    Edx,
};

struct GameplayCadenceHookSemantics {
    std::uint32_t authored_period{};
    GameplayCadenceTestRegister test_register{};
    bool has_signed_phase{};
};

struct GameplaySongClockInputSelection {
    GameplaySongClockInputState state{
        GameplaySongClockInputState::Failed};
    std::optional<SongClockObservation> observation{};
    std::uint64_t output_frame{};
};

struct GameplaySongClockStepSelection {
    GameplaySongClockInputSelection input{};
    std::optional<GameplaySongClockDecision> decision{};
    std::uint32_t step{1};
    bool observation_rejected{};
};

[[nodiscard]] GameplaySongClockInputSelection
SelectGameplaySongClockInput(
    int group_cursor_ms,
    const std::optional<audio::GameplayAudioCursorObservation>&
        cursor_observation) noexcept;

[[nodiscard]] GameplaySongClockStepSelection
ResolveGameplaySongClockStep(
    GameplaySongClock& clock,
    std::uint32_t current_tick,
    std::int32_t game_time_offset_ms,
    int group_cursor_ms,
    const std::optional<audio::GameplayAudioCursorObservation>&
        cursor_observation) noexcept;

[[nodiscard]] std::expected<bool, FramerateProfileError>
ShouldRunGameplayCadence(
    const FramerateProfile& profile,
    GameplayAudioClockPlan audio_clock_plan,
    std::uint32_t current_tick,
    std::uint32_t step,
    std::int32_t phase,
    std::uint32_t authored_period) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
CountGameplayEffectAdvances(
    const FramerateProfile& profile,
    GameplayAudioClockPlan audio_clock_plan,
    std::uint32_t current_tick,
    std::uint32_t step) noexcept;

[[nodiscard]] std::optional<GameplayCadenceHookSemantics>
GetGameplayCadenceHookSemantics(
    FramerateHookId id) noexcept;

} // namespace detail

} // namespace gc::framerate
