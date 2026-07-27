#include "Audio/DirectSound/GameplayAudioCursorObservation.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

using gc::audio::GameplayAudioCursorObservation;
using gc::audio::GameplayAudioCursorState;
using gc::audio::PublishGameplayAudioCursorObservation;
using gc::audio::ScopedGameplayAudioCursorQuery;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

GameplayAudioCursorObservation Exact(std::uint64_t source_frame) noexcept {
    return {
        .state = GameplayAudioCursorState::Exact,
        .source_frame_unwrapped = source_frame,
        .source_sample_rate = 44'100,
        .playback_generation = 17,
        .output_frame = 48'000,
    };
}

int TestUnscopedPublicationIsIgnored() {
    PublishGameplayAudioCursorObservation(Exact(100));
    ScopedGameplayAudioCursorQuery query;
    return Expect(
        !query.Consume().has_value(),
        "an unscoped publication to be ignored");
}

int TestOwnedScopeConsumesFreshPublicationOnce() {
    ScopedGameplayAudioCursorQuery query;
    PublishGameplayAudioCursorObservation(Exact(123'456));

    const auto observation = query.Consume();
    int failures = 0;
    failures += Expect(
        observation.has_value(),
        "the owning scope to consume its publication");
    failures += Expect(
        observation.has_value() &&
            observation->query_serial != 0 &&
            observation->state == GameplayAudioCursorState::Exact &&
            observation->source_frame_unwrapped == 123'456 &&
            observation->source_sample_rate == 44'100 &&
            observation->playback_generation == 17 &&
            observation->output_frame == 48'000,
        "the exact observation to preserve cursor metadata");
    failures += Expect(
        !query.Consume().has_value(),
        "a publication to be consumable only once");
    return failures;
}

int TestMostRecentPublicationWinsWithinScope() {
    ScopedGameplayAudioCursorQuery query;
    PublishGameplayAudioCursorObservation(Exact(10));
    PublishGameplayAudioCursorObservation(Exact(20));
    const auto observation = query.Consume();
    return Expect(
        observation.has_value() &&
            observation->source_frame_unwrapped == 20,
        "the most recent publication in one query to win");
}

int TestStalePublicationDoesNotCrossScopes() {
    {
        ScopedGameplayAudioCursorQuery first;
        PublishGameplayAudioCursorObservation(Exact(300));
    }

    ScopedGameplayAudioCursorQuery second;
    return Expect(
        !second.Consume().has_value(),
        "an unconsumed publication to be cleared with its scope");
}

int TestNestedScopeCannotConsumeOrClearOuterResult() {
    ScopedGameplayAudioCursorQuery outer;
    PublishGameplayAudioCursorObservation(Exact(400));
    {
        ScopedGameplayAudioCursorQuery nested;
        if (nested.Consume().has_value()) {
            return Expect(false, "a nested scope not to consume outer data");
        }
    }

    const auto observation = outer.Consume();
    return Expect(
        observation.has_value() &&
            observation->source_frame_unwrapped == 400,
        "nested destruction not to clear the outer publication");
}

int TestOtherThreadCannotPublishIntoScope() {
    ScopedGameplayAudioCursorQuery query;
    std::thread publisher([] {
        PublishGameplayAudioCursorObservation(Exact(500));
    });
    publisher.join();
    return Expect(
        !query.Consume().has_value(),
        "another thread not to publish into this thread's scope");
}

int TestInactivePublicationRetainsItsState() {
    ScopedGameplayAudioCursorQuery query;
    PublishGameplayAudioCursorObservation({
        .state = GameplayAudioCursorState::Inactive,
        .source_sample_rate = 48'000,
        .playback_generation = 23,
    });
    const auto observation = query.Consume();
    return Expect(
        observation.has_value() &&
            observation->state == GameplayAudioCursorState::Inactive &&
            observation->query_serial != 0 &&
            observation->source_sample_rate == 48'000 &&
            observation->playback_generation == 23,
        "an inactive observation to remain distinct and fresh");
}

} // namespace

int main() {
    int failures = 0;
    failures += TestUnscopedPublicationIsIgnored();
    failures += TestOwnedScopeConsumesFreshPublicationOnce();
    failures += TestMostRecentPublicationWinsWithinScope();
    failures += TestStalePublicationDoesNotCrossScopes();
    failures += TestNestedScopeCannotConsumeOrClearOuterResult();
    failures += TestOtherThreadCannotPublishIntoScope();
    failures += TestInactivePublicationRetainsItsState();
    return failures == 0 ? 0 : 1;
}
