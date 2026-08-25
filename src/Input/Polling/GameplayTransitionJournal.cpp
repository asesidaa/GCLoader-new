#include "Input/Polling/GameplayTransitionJournal.h"

#include "Input/Polling/InputSnapshotState.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <limits>
#include <mutex>

namespace gc::input {
namespace {

struct GameplayTransitionTransport {
    std::array<GameplayTransitionRecord, kGameplayTransitionCapacity> records{};
    std::mutex mutex;
    std::size_t read_slot{};
    std::size_t size{};
    std::uint64_t transport_epoch{};
    std::uint64_t next_sequence{};
    std::uint64_t eviction_count{};
    bool enabled{};
    bool active{};
    GameplayHeldMask published_held{};
    std::int64_t qpc_frequency{};
};

GameplayTransitionTransport& Transport() noexcept
{
    static GameplayTransitionTransport transport;
    return transport;
}

void IncrementEvictionCount(GameplayTransitionTransport& transport) noexcept
{
    if (transport.eviction_count !=
        (std::numeric_limits<std::uint64_t>::max)())
    {
        ++transport.eviction_count;
    }
}

void ClearQueue(GameplayTransitionTransport& transport) noexcept
{
    transport.read_slot = 0;
    transport.size = 0;
}

GameplayTransitionStatus StatusOf(
    const GameplayTransitionTransport& transport) noexcept
{
    return GameplayTransitionStatus{
        .enabled = transport.enabled,
        .active = transport.active,
        .transport_epoch = transport.transport_epoch,
        .next_sequence = transport.next_sequence,
        .eviction_count = transport.eviction_count,
        .depth = static_cast<std::uint32_t>(transport.size),
        .published_held = transport.published_held,
        .qpc_frequency = transport.qpc_frequency,
    };
}

}

bool PrepareGameplayTransitionTransport(bool enabled) noexcept
{
    LARGE_INTEGER frequency{};
    if (enabled &&
        (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0))
    {
        auto& transport = Transport();
        std::lock_guard lock(transport.mutex);
        ClearQueue(transport);
        transport.transport_epoch = 0;
        transport.next_sequence = 0;
        transport.eviction_count = 0;
        transport.enabled = false;
        transport.active = false;
        transport.published_held = 0;
        transport.qpc_frequency = 0;
        return false;
    }

    auto& transport = Transport();
    std::lock_guard lock(transport.mutex);
    ClearQueue(transport);
    transport.transport_epoch = 0;
    transport.next_sequence = 0;
    transport.eviction_count = 0;
    transport.enabled = enabled;
    transport.active = false;
    transport.published_held = 0;
    transport.qpc_frequency = enabled ? frequency.QuadPart : 0;
    return true;
}

void BeginGameplayTransitionEpoch(GameplayHeldMask baseline) noexcept
{
    auto& transport = Transport();
    std::lock_guard lock(transport.mutex);
    if (!transport.enabled)
    {
        return;
    }

    ClearQueue(transport);
    transport.next_sequence = 0;
    transport.published_held = baseline;
    if (transport.transport_epoch ==
        (std::numeric_limits<std::uint64_t>::max)())
    {
        IncrementEvictionCount(transport);
        transport.active = false;
        return;
    }

    ++transport.transport_epoch;
    transport.active = true;
}

void EndGameplayTransitionEpoch() noexcept
{
    auto& transport = Transport();
    std::lock_guard lock(transport.mutex);
    transport.active = false;
}

bool CaptureGameplayTransitionCutoff(
    const gc::timing::AbsoluteHostTime& stage_entry_time,
    GameplayTransitionCutoff* output) noexcept
{
    if (output == nullptr || stage_entry_time.qpc_ticks <= 0)
    {
        return false;
    }

    auto& transport = Transport();
    std::lock_guard lock(transport.mutex);
    if (!transport.enabled || !transport.active)
    {
        return false;
    }

    std::uint64_t handoff_drops{};
    for (std::size_t index = 0; index < transport.size; ++index)
    {
        const auto& record = transport.records[
            (transport.read_slot + index) % kGameplayTransitionCapacity];
        if (record.observed_time.qpc_ticks >= stage_entry_time.qpc_ticks)
        {
            ++handoff_drops;
        }
    }

    *output = GameplayTransitionCutoff{
        .transport_epoch = transport.transport_epoch,
        .first_stage_sequence = transport.next_sequence,
        .eviction_count = transport.eviction_count,
        .held_baseline = transport.published_held,
        .qpc_frequency = transport.qpc_frequency,
        .stage_entry_time = stage_entry_time,
        .stage_entry_handoff_drops = handoff_drops,
    };
    ClearQueue(transport);
    return true;
}

void PublishGameplayTransition(
    std::uint32_t previous_fastio,
    std::uint32_t next_fastio,
    const gc::timing::AbsoluteHostTime& observed_time,
    const std::optional<std::uint32_t> raw_message_queue_age_ms) noexcept
{
    const GameplayHeldMask previous =
        GameplayMaskFromFastIo(previous_fastio);
    const GameplayHeldMask next = GameplayMaskFromFastIo(next_fastio);
    if (previous == next)
    {
        return;
    }

    auto& transport = Transport();
    std::lock_guard lock(transport.mutex);
    if (!transport.enabled || !transport.active)
    {
        return;
    }
    if (transport.next_sequence ==
        (std::numeric_limits<std::uint64_t>::max)())
    {
        IncrementEvictionCount(transport);
        transport.active = false;
        return;
    }

    if (transport.size == kGameplayTransitionCapacity)
    {
        transport.read_slot =
            (transport.read_slot + 1) % kGameplayTransitionCapacity;
        --transport.size;
        IncrementEvictionCount(transport);
    }

    const std::size_t write_slot =
        (transport.read_slot + transport.size) %
        kGameplayTransitionCapacity;
    transport.records[write_slot] = GameplayTransitionRecord{
        .transport_epoch = transport.transport_epoch,
        .sequence = transport.next_sequence,
        .observed_time = observed_time,
        .raw_message_queue_age_available =
            raw_message_queue_age_ms.has_value(),
        .raw_message_queue_age_ms =
            raw_message_queue_age_ms.value_or(0),
        .held_before = previous,
        .held_after = next,
        .rising = static_cast<GameplayHeldMask>(next & ~previous),
        .falling = static_cast<GameplayHeldMask>(previous & ~next),
    };
    ++transport.next_sequence;
    ++transport.size;
    transport.published_held = next;
}

std::size_t DrainGameplayTransitions(
    std::span<GameplayTransitionRecord> output,
    GameplayTransitionStatus* status) noexcept
{
    auto& transport = Transport();
    std::lock_guard lock(transport.mutex);
    const std::size_t count = std::min(output.size(), transport.size);
    for (std::size_t index = 0; index < count; ++index)
    {
        output[index] = transport.records[
            (transport.read_slot + index) % kGameplayTransitionCapacity];
    }
    transport.read_slot =
        (transport.read_slot + count) % kGameplayTransitionCapacity;
    transport.size -= count;

    if (status != nullptr)
    {
        *status = StatusOf(transport);
    }
    return count;
}

GameplayTransitionStatus ReadGameplayTransitionStatus() noexcept
{
    auto& transport = Transport();
    std::lock_guard lock(transport.mutex);
    return StatusOf(transport);
}

GameplayHeldMask GameplayMaskFromFastIo(std::uint32_t word) noexcept
{
    GameplayHeldMask result = 0;
    for (std::size_t logical_index = 0;
         logical_index < kGameplayLogicalInputCount;
         ++logical_index)
    {
        if ((word & kFastIoMaskByLogicalAction[logical_index]) != 0)
        {
            result |= static_cast<GameplayHeldMask>(1u << logical_index);
        }
    }
    return result;
}

}
