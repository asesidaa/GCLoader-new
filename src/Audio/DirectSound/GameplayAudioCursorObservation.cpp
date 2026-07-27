#include "Audio/DirectSound/GameplayAudioCursorObservation.h"

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
ScopedGameplayAudioCursorQuery::Consume() noexcept {
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

} // namespace gc::audio
