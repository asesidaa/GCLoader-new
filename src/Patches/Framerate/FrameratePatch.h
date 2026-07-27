#pragma once

#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Patches/Framerate/GameplaySongClock.h"

#include <cstdint>
#include <optional>

namespace gc::framerate {

enum class FramerateHookId;

[[nodiscard]] bool FramerateHookHasRuntimeBinding(
    FramerateHookId id) noexcept;
[[nodiscard]] bool FrameratePatchInit(bool wasapi_audio_committed);

namespace detail {

enum class GameplaySongClockInputState : std::uint8_t {
    Exact,
    Rounded,
    Inactive,
    Failed,
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
    std::optional<audio::GameplayAudioCursorObservation>
        cursor_observation) noexcept;

[[nodiscard]] GameplaySongClockStepSelection
ResolveGameplaySongClockStep(
    GameplaySongClock& clock,
    std::uint32_t current_tick,
    std::int32_t game_time_offset_ms,
    int group_cursor_ms,
    std::optional<audio::GameplayAudioCursorObservation>
        cursor_observation) noexcept;

void PublishAudioResyncDiagnostic(
    std::int32_t drift_ms,
    std::int32_t margin_ms,
    bool readable,
    bool suppressed) noexcept;

} // namespace detail

} // namespace gc::framerate
