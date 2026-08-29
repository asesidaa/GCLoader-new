#pragma once

#include "Audio/Mixer/AudioCursorTimeline.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace gc::audio {

enum class GameplayAudioCursorState : std::uint8_t {
    Exact,
    Pending,
    Inactive,
};

struct GameplayAudioCursorObservation {
    std::uint64_t query_serial{};
    GameplayAudioCursorState state{};
    std::uint64_t source_frame_unwrapped{};
    std::uint32_t source_sample_rate{};
    std::uint64_t buffer_instance_id{};
    std::uint64_t timeline_generation{};
    std::uint64_t playback_generation{};
    ExactPlaybackOrigin origin{};
    std::uint64_t output_frame{};
    std::shared_ptr<AudioCursorTimeline> exact_history;
};

class ScopedGameplayAudioCursorQuery final {
public:
    ScopedGameplayAudioCursorQuery() noexcept;
    ~ScopedGameplayAudioCursorQuery();

    ScopedGameplayAudioCursorQuery(
        const ScopedGameplayAudioCursorQuery&) = delete;
    ScopedGameplayAudioCursorQuery& operator=(
        const ScopedGameplayAudioCursorQuery&) = delete;
    ScopedGameplayAudioCursorQuery(
        ScopedGameplayAudioCursorQuery&&) = delete;
    ScopedGameplayAudioCursorQuery& operator=(
        ScopedGameplayAudioCursorQuery&&) = delete;

    [[nodiscard]] std::optional<GameplayAudioCursorObservation>
    Consume() noexcept;

private:
    std::uint64_t serial_{};
    bool owns_scope_{};
};

void PublishGameplayAudioCursorObservation(
    GameplayAudioCursorObservation observation) noexcept;

} // namespace gc::audio
