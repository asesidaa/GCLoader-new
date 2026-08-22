#pragma once

#include "Timing/AbsoluteHostTime.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gc::input {

using GameplayHeldMask = std::uint16_t;
inline constexpr std::size_t kGameplayTransitionCapacity = 65'536;

struct GameplayTransitionRecord {
    std::uint64_t transport_epoch{};
    std::uint64_t sequence{};
    gc::timing::AbsoluteHostTime observed_time{};
    bool raw_message_queue_age_available{};
    std::uint32_t raw_message_queue_age_ms{};
    GameplayHeldMask held_before{};
    GameplayHeldMask held_after{};
    GameplayHeldMask rising{};
    GameplayHeldMask falling{};
};

struct GameplayTransitionStatus {
    bool enabled{};
    bool active{};
    std::uint64_t transport_epoch{};
    std::uint64_t next_sequence{};
    std::uint64_t eviction_count{};
    std::uint32_t depth{};
    GameplayHeldMask published_held{};
    std::int64_t qpc_frequency{};
};

struct GameplayTransitionCutoff {
    std::uint64_t transport_epoch{};
    std::uint64_t first_stage_sequence{};
    std::uint64_t eviction_count{};
    GameplayHeldMask held_baseline{};
    std::int64_t qpc_frequency{};
    gc::timing::AbsoluteHostTime stage_entry_time{};
    std::uint64_t stage_entry_handoff_drops{};
};

bool PrepareGameplayTransitionTransport(bool enabled) noexcept;
void BeginGameplayTransitionEpoch(GameplayHeldMask baseline) noexcept;
void EndGameplayTransitionEpoch() noexcept;
bool CaptureGameplayTransitionCutoff(
    gc::timing::AbsoluteHostTime stage_entry_time,
    GameplayTransitionCutoff* output) noexcept;
void PublishGameplayTransition(
    std::uint32_t previous_fastio,
    std::uint32_t next_fastio,
    gc::timing::AbsoluteHostTime observed_time,
    std::optional<std::uint32_t> raw_message_queue_age_ms =
        std::nullopt) noexcept;
std::size_t DrainGameplayTransitions(
    std::span<GameplayTransitionRecord> output,
    GameplayTransitionStatus* status) noexcept;
GameplayTransitionStatus ReadGameplayTransitionStatus() noexcept;
GameplayHeldMask GameplayMaskFromFastIo(std::uint32_t word) noexcept;

}
