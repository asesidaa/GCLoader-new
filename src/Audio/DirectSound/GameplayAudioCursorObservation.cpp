#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Audio/AudioContractFatal.h"

#include <atomic>
#include <limits>

namespace gc::audio {
namespace {

struct GameplayCursorQueryState {
    std::uint64_t next_serial{};
    std::uint64_t active_serial{};
    bool active{};
    std::optional<GameplayAudioCursorObservation> publication;
};

thread_local GameplayCursorQueryState g_query_state;
std::atomic_uint64_t g_logical_play_order{};

std::uint64_t NextSerial() noexcept {
    if (g_query_state.next_serial ==
        std::numeric_limits<std::uint64_t>::max()) {
        g_query_state.next_serial = 1;
    } else {
        ++g_query_state.next_serial;
        if (g_query_state.next_serial == 0) {
            g_query_state.next_serial = 1;
        }
    }
    return g_query_state.next_serial;
}

} // namespace

ScopedGameplayAudioCursorQuery::ScopedGameplayAudioCursorQuery() noexcept {
    if (g_query_state.active) {
        return;
    }

    serial_ = NextSerial();
    owns_scope_ = true;
    g_query_state.active = true;
    g_query_state.active_serial = serial_;
    g_query_state.publication.reset();
}

ScopedGameplayAudioCursorQuery::~ScopedGameplayAudioCursorQuery() {
    if (!owns_scope_ || !g_query_state.active ||
        g_query_state.active_serial != serial_) {
        return;
    }

    g_query_state.publication.reset();
    g_query_state.active_serial = 0;
    g_query_state.active = false;
}

std::optional<GameplayAudioCursorObservation>
// Consume mutates the scoped thread-local publication.
// ReSharper disable once CppMemberFunctionMayBeConst
// NOLINTNEXTLINE(readability-make-member-function-const)
ScopedGameplayAudioCursorQuery::Consume() noexcept {
    // Binary authority: native VA 0x6122B0 walks the ordered group channel
    // list, calls the cursor method with one output slot, and breaks after the
    // first successful call at 0x61233F..0x612368. The last scoped publication
    // therefore belongs to the native-selected successful channel; no second
    // loader-side voice selection policy is permitted here.
    if (!owns_scope_ || !g_query_state.active ||
        g_query_state.active_serial != serial_ ||
        !g_query_state.publication.has_value() ||
        g_query_state.publication->query_serial != serial_) {
        return std::nullopt;
    }

    auto publication = g_query_state.publication;
    g_query_state.publication.reset();
    return publication;
}

void PublishGameplayAudioCursorObservation(
    GameplayAudioCursorObservation observation) noexcept {
    if (!g_query_state.active || g_query_state.active_serial == 0) {
        return;
    }

    observation.query_serial = g_query_state.active_serial;
    g_query_state.publication = observation;
}

std::uint64_t SnapshotLogicalPlayOrder() noexcept {
    return g_logical_play_order.load(std::memory_order_seq_cst);
}

std::uint64_t ClaimNextLogicalPlayOrder() noexcept {
    const auto previous =
        g_logical_play_order.fetch_add(1, std::memory_order_seq_cst);
    if (previous == std::numeric_limits<std::uint64_t>::max()) {
        FailAudioContract(
            AudioContractFatalReason::LogicalPlayOrderExhausted,
            previous);
    }
    return previous + 1;
}

} // namespace gc::audio
