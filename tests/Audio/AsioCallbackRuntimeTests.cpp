// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioCallbackRuntime.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <thread>

namespace {

using gc::audio::AsioCallbackRuntime;
using gc::audio::AsioCallbackRuntimeActions;
using gc::audio::AsioFailureStage;
using gc::audio::AsioLegacyPositionActions;
using gc::audio::AsioRenderRequest;
using gc::audio::IAsioBlockRenderer;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

void SetSamples(ASIOSamples& destination, std::uint64_t value) noexcept {
#if NATIVE_INT64
    destination = static_cast<ASIOSamples>(value);
#else
    destination.hi = static_cast<unsigned long>(value >> 32U);
    destination.lo = static_cast<unsigned long>(value);
#endif
}

void SetTimestamp(ASIOTimeStamp& destination, std::uint64_t value) noexcept {
#if NATIVE_INT64
    destination = static_cast<ASIOTimeStamp>(value);
#else
    destination.hi = static_cast<unsigned long>(value >> 32U);
    destination.lo = static_cast<unsigned long>(value);
#endif
}

void MakeSamplesNegative(ASIOSamples& destination) noexcept {
#if NATIVE_INT64
    destination = -1;
#else
    destination.hi = 0xFFFFFFFFUL;
    destination.lo = 0xFFFFFFFFUL;
#endif
}

void MakeTimestampNegative(ASIOTimeStamp& destination) noexcept {
#if NATIVE_INT64
    destination = -1;
#else
    destination.hi = 0xFFFFFFFFUL;
    destination.lo = 0xFFFFFFFFUL;
#endif
}

ASIOTime MakeTime(
    std::uint64_t sample_position,
    std::uint64_t system_time_ns) noexcept {
    ASIOTime time{};
    time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;
    time.timeInfo.sampleRate = 48'000.0;
    time.timeInfo.speed = 1.0;
    SetSamples(time.timeInfo.samplePosition, sample_position);
    SetTimestamp(time.timeInfo.systemTime, system_time_ns);
    return time;
}

struct FakeActionsState {
    std::atomic_uint64_t qpc_ticks{100};
    std::atomic_uint32_t qpc_calls{};
    std::atomic_uint32_t frequency_calls{};
    std::atomic_uint32_t promote_calls{};
    std::atomic_uint32_t revert_calls{};
    std::atomic_bool promoted_as_pro_audio{};
    bool qpc_succeeds{true};
    bool frequency_succeeds{true};
    bool promotion_succeeds{true};

    ASIOError legacy_result{ASE_OK};
    ASIOSamples legacy_samples{};
    ASIOTimeStamp legacy_timestamp{};
    std::atomic_uint32_t legacy_calls{};
};

bool FakeQueryPerformanceCounter(
    void* context,
    std::uint64_t* value) noexcept {
    auto& state = *static_cast<FakeActionsState*>(context);
    ++state.qpc_calls;
    if (!state.qpc_succeeds) {
        return false;
    }
    *value = state.qpc_ticks.fetch_add(10);
    return true;
}

bool FakeQueryPerformanceFrequency(
    void* context,
    std::uint64_t* value) noexcept {
    auto& state = *static_cast<FakeActionsState*>(context);
    ++state.frequency_calls;
    if (!state.frequency_succeeds) {
        return false;
    }
    *value = 10'000'000;
    return true;
}

void* FakePromoteWorker(
    void* context,
    const wchar_t* task_name,
    std::uint32_t*) noexcept {
    auto& state = *static_cast<FakeActionsState*>(context);
    ++state.promote_calls;
    state.promoted_as_pro_audio.store(
        task_name != nullptr && wcscmp(task_name, L"Pro Audio") == 0);
    return state.promotion_succeeds
        ? reinterpret_cast<void*>(static_cast<std::uintptr_t>(1))
        : nullptr;
}

bool FakeRevertWorker(void* context, void*) noexcept {
    auto& state = *static_cast<FakeActionsState*>(context);
    ++state.revert_calls;
    return true;
}

ASIOError FakeGetSamplePosition(
    void* context,
    ASIOSamples* sample_position,
    ASIOTimeStamp* timestamp) noexcept {
    auto& state = *static_cast<FakeActionsState*>(context);
    ++state.legacy_calls;
    *sample_position = state.legacy_samples;
    *timestamp = state.legacy_timestamp;
    return state.legacy_result;
}

AsioCallbackRuntimeActions RuntimeActions(
    FakeActionsState& state) noexcept {
    return {
        &state,
        &FakeQueryPerformanceCounter,
        &FakeQueryPerformanceFrequency,
        &FakePromoteWorker,
        &FakeRevertWorker,
    };
}

AsioLegacyPositionActions LegacyActions(
    FakeActionsState& state) noexcept {
    return {&state, &FakeGetSamplePosition};
}

class FakeRenderer final : public IAsioBlockRenderer {
public:
    FakeRenderer() {
        render_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        render_allowed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        render_finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    ~FakeRenderer() override {
        CloseHandle(render_finished);
        CloseHandle(render_allowed);
        CloseHandle(render_entered);
    }

    void RenderAsioBlock(const AsioRenderRequest& request) noexcept override {
        const auto active_now = active_renders.fetch_add(1) + 1;
        auto maximum = maximum_concurrent_renders.load();
        while (maximum < active_now &&
               !maximum_concurrent_renders.compare_exchange_weak(
                   maximum,
                   active_now)) {
        }

        last_request = request;
        render_thread_id.store(GetCurrentThreadId());
        ++render_count;
        SetEvent(render_entered);
        if (block_render.load()) {
            WaitForSingleObject(render_allowed, 2'000);
        }
        --active_renders;
        SetEvent(render_finished);
    }

    void ClearAsioBlock(long buffer_index) noexcept override {
        if (buffer_index >= 0 && buffer_index < 2) {
            ++clear_counts[static_cast<std::size_t>(buffer_index)];
        }
        ++clear_count;
    }

    void OnAsioRuntimeFault(AsioFailureStage stage) noexcept override {
        AsioFailureStage expected = AsioFailureStage::none;
        first_fault.compare_exchange_strong(expected, stage);
        ++fault_count;
    }

    void AllowRender() noexcept {
        SetEvent(render_allowed);
    }

    HANDLE render_entered{};
    HANDLE render_allowed{};
    HANDLE render_finished{};
    std::atomic_bool block_render{};
    std::atomic_uint32_t render_count{};
    std::atomic_uint32_t clear_count{};
    std::array<std::atomic_uint32_t, 2> clear_counts{};
    std::atomic_uint32_t fault_count{};
    std::atomic_uint32_t active_renders{};
    std::atomic_uint32_t maximum_concurrent_renders{};
    std::atomic<DWORD> render_thread_id{};
    std::atomic<AsioFailureStage> first_fault{AsioFailureStage::none};
    AsioRenderRequest last_request{};
};

std::unique_ptr<AsioCallbackRuntime> PrepareRuntime(
    FakeRenderer& renderer,
    FakeActionsState& actions_state) {
    auto prepared = AsioCallbackRuntime::Prepare(
        renderer,
        LegacyActions(actions_state),
        RuntimeActions(actions_state));
    if (!prepared.has_value()) {
        std::cerr << "Runtime preparation failed at stage "
                  << static_cast<int>(prepared.error().stage) << '\n';
        return nullptr;
    }
    return std::move(*prepared);
}

bool Install(AsioCallbackRuntime& runtime) {
    const auto result = runtime.Install();
    return result.has_value();
}

void Shutdown(AsioCallbackRuntime& runtime) noexcept {
    runtime.BeginStopping();
    runtime.JoinWorker();
    runtime.Uninstall();
}

int TestCallbacksAreInertOutsideInstallation() {
    auto* callbacks = AsioCallbackRuntime::Callbacks();
    auto time = MakeTime(0, 1'000'000);

    int failures{};
    failures += Expect(
        callbacks->bufferSwitchTimeInfo(&time, 0, ASIOTrue) == &time,
        "time-info callback returns incoming pointer while inert");
    callbacks->bufferSwitch(0, ASIOTrue);
    callbacks->sampleRateDidChange(48'000.0);
    failures += Expect(
        callbacks->asioMessage(kAsioEngineVersion, 0, nullptr, nullptr) == 0,
        "message callback is inert without an installed runtime");

    FakeRenderer renderer;
    FakeActionsState actions;
    auto runtime = PrepareRuntime(renderer, actions);
    failures += Expect(runtime != nullptr, "runtime prepares");
    if (runtime == nullptr) {
        return failures;
    }
    failures += Expect(Install(*runtime), "runtime installs");
    Shutdown(*runtime);

    callbacks->bufferSwitchTimeInfo(&time, 0, ASIOTrue);
    callbacks->bufferSwitch(0, ASIOTrue);
    failures += Expect(
        renderer.render_count.load() == 0 &&
            renderer.clear_count.load() == 0 &&
            actions.legacy_calls.load() == 0,
        "callbacks after uninstall do not dereference stale state");
    return failures;
}

int TestInlineTimeInfoAndLegacyCallbacks() {
    FakeRenderer renderer;
    FakeActionsState actions;
    SetSamples(actions.legacy_samples, 576);
    SetTimestamp(actions.legacy_timestamp, 8'000'000);
    auto runtime = PrepareRuntime(renderer, actions);
    if (runtime == nullptr) {
        return 1;
    }
    int failures = Expect(Install(*runtime), "inline runtime installs");

    auto time = MakeTime(384, 4'000'000);
    auto* callbacks = AsioCallbackRuntime::Callbacks();
    const auto* returned =
        callbacks->bufferSwitchTimeInfo(&time, 1, ASIOTrue);
    failures += Expect(returned == &time, "time-info callback returns input");
    failures += Expect(
        renderer.render_count.load() == 1 &&
            renderer.last_request.buffer_index == 1 &&
            renderer.last_request.direct_process == ASIOTrue &&
            renderer.last_request.has_time_info &&
            renderer.last_request.sample_position == 384 &&
            renderer.last_request.system_time_ns == 4'000'000,
        "time-info callback renders exact supplied position pair inline");
    failures += Expect(
        actions.legacy_calls.load() == 0,
        "time-info callback never mixes in legacy position values");

    callbacks->bufferSwitch(0, ASIOTrue);
    failures += Expect(
        renderer.render_count.load() == 2 &&
            renderer.last_request.buffer_index == 0 &&
            !renderer.last_request.has_time_info &&
            renderer.last_request.sample_position == 576 &&
            renderer.last_request.system_time_ns == 8'000'000,
        "legacy callback renders exact getSamplePosition pair inline");

    const auto snapshot = runtime->Snapshot();
    failures += Expect(
        snapshot.callbacks == 2 &&
            snapshot.time_info_callbacks == 1 &&
            snapshot.legacy_callbacks == 1 &&
            snapshot.maximum_callback_ticks == 10 &&
            snapshot.qpc_frequency == 10'000'000,
        "inline callback counters and cached timer frequency are published");
    failures += Expect(
        actions.frequency_calls.load() == 1 &&
            actions.promote_calls.load() == 1 &&
            actions.promoted_as_pro_audio.load(),
        "worker is ready and promoted as Pro Audio before installation");

    Shutdown(*runtime);
    failures += Expect(
        actions.revert_calls.load() == 1,
        "worker reverts MMCSS registration when joined");
    return failures;
}

int TestDeferredRequestIsBounded() {
    FakeRenderer renderer;
    renderer.block_render.store(true);
    FakeActionsState actions;
    auto runtime = PrepareRuntime(renderer, actions);
    if (runtime == nullptr) {
        return 1;
    }
    int failures = Expect(Install(*runtime), "deferred runtime installs");
    auto first = MakeTime(0, 1'000'000);
    auto second = MakeTime(192, 5'000'000);
    const auto caller_thread = GetCurrentThreadId();

    AsioCallbackRuntime::Callbacks()->bufferSwitchTimeInfo(
        &first,
        0,
        ASIOFalse);
    failures += Expect(
        WaitForSingleObject(renderer.render_entered, 2'000) == WAIT_OBJECT_0,
        "deferred worker consumes the published slot");
    failures += Expect(
        renderer.render_thread_id.load() != caller_thread,
        "ASIOFalse returns work to a pre-created worker");

    const auto clears_before = renderer.clear_count.load();
    AsioCallbackRuntime::Callbacks()->bufferSwitchTimeInfo(
        &second,
        1,
        ASIOFalse);
    const auto snapshot_while_blocked = runtime->Snapshot();
    failures += Expect(
        renderer.render_count.load() == 1 &&
            renderer.clear_count.load() == clears_before &&
            snapshot_while_blocked.deadline_misses == 1 &&
            snapshot_while_blocked.first_fault == AsioFailureStage::callback,
        "second deferred request is neither overwritten nor dereferenced");

    renderer.AllowRender();
    failures += Expect(
        WaitForSingleObject(renderer.render_finished, 2'000) == WAIT_OBJECT_0,
        "deferred render completes after release");
    const auto final_snapshot = runtime->Snapshot();
    failures += Expect(
        final_snapshot.deferred_callbacks == 2 &&
            final_snapshot.maximum_render_ticks > 0 &&
            renderer.maximum_concurrent_renders.load() == 1,
        "deferred duration and single-render bound are recorded");

    Shutdown(*runtime);
    return failures;
}

int TestOverlappingInlineAndDeferredCallbacksLoseClaim() {
    int failures{};
    for (const auto overlap_direct : {ASIOTrue, ASIOFalse}) {
        FakeRenderer renderer;
        renderer.block_render.store(true);
        FakeActionsState actions;
        auto runtime = PrepareRuntime(renderer, actions);
        if (runtime == nullptr) {
            ++failures;
            continue;
        }
        failures += Expect(Install(*runtime), "overlap runtime installs");
        auto first = MakeTime(0, 1'000'000);
        auto second = MakeTime(192, 5'000'000);

        std::thread active_callback([&] {
            AsioCallbackRuntime::Callbacks()->bufferSwitchTimeInfo(
                &first,
                0,
                ASIOTrue);
        });
        failures += Expect(
            WaitForSingleObject(renderer.render_entered, 2'000) ==
                WAIT_OBJECT_0,
            "inline renderer enters before overlap");
        AsioCallbackRuntime::Callbacks()->bufferSwitchTimeInfo(
            &second,
            1,
            overlap_direct);
        failures += Expect(
            renderer.render_count.load() == 1 &&
                renderer.clear_count.load() == 0 &&
                runtime->Snapshot().deadline_misses == 1,
            "overlapping callback performs no renderer dereference");
        renderer.AllowRender();
        active_callback.join();
        failures += Expect(
            renderer.maximum_concurrent_renders.load() == 1,
            "renderer is never entered concurrently");
        Shutdown(*runtime);
    }
    return failures;
}

int TestBufferAlternationAndStopping() {
    FakeRenderer renderer;
    FakeActionsState actions;
    auto runtime = PrepareRuntime(renderer, actions);
    if (runtime == nullptr) {
        return 1;
    }
    int failures = Expect(Install(*runtime), "alternation runtime installs");
    auto a = MakeTime(0, 1'000'000);
    auto b = MakeTime(192, 5'000'000);
    auto c = MakeTime(384, 9'000'000);
    auto* callbacks = AsioCallbackRuntime::Callbacks();

    callbacks->bufferSwitchTimeInfo(&a, 1, ASIOTrue);
    callbacks->bufferSwitchTimeInfo(&b, 0, ASIOTrue);
    failures += Expect(
        renderer.render_count.load() == 2,
        "either first buffer index is legal and alternating index renders");
    callbacks->bufferSwitchTimeInfo(&c, 0, ASIOTrue);
    failures += Expect(
        renderer.render_count.load() == 2 &&
            renderer.clear_counts[0].load() == 1 &&
            runtime->Snapshot().buffer_alternation_violations == 1,
        "repeated valid buffer is cleared and faulted");

    const auto clears_before_out_of_range = renderer.clear_count.load();
    callbacks->bufferSwitchTimeInfo(&c, 2, ASIOTrue);
    failures += Expect(
        renderer.clear_count.load() == clears_before_out_of_range,
        "out-of-range index never dereferences a driver buffer");

    Shutdown(*runtime);

    FakeRenderer stopping_renderer;
    FakeActionsState stopping_actions;
    auto stopping_runtime = PrepareRuntime(stopping_renderer, stopping_actions);
    if (stopping_runtime == nullptr) {
        return failures + 1;
    }
    failures += Expect(
        Install(*stopping_runtime),
        "stopping runtime installs");
    stopping_runtime->BeginStopping();
    callbacks->bufferSwitchTimeInfo(&a, 0, ASIOTrue);
    failures += Expect(
        stopping_renderer.render_count.load() == 0 &&
            stopping_renderer.clear_counts[0].load() == 1,
        "callback after BeginStopping writes silence through live renderer");
    stopping_runtime->JoinWorker();
    stopping_runtime->Uninstall();
    return failures;
}

void MissingFlags(ASIOTime& time) noexcept {
    time.timeInfo.flags = kSamplePositionValid;
}

void NegativeSamples(ASIOTime& time) noexcept {
    MakeSamplesNegative(time.timeInfo.samplePosition);
}

void NegativeTimestamp(ASIOTime& time) noexcept {
    MakeTimestampNegative(time.timeInfo.systemTime);
}

void ChangedRate(ASIOTime& time) noexcept {
    time.timeInfo.flags |= kSampleRateChanged | kSampleRateValid;
}

void WrongRate(ASIOTime& time) noexcept {
    time.timeInfo.flags |= kSampleRateValid;
    time.timeInfo.sampleRate = 44'100.0;
}

void WrongSpeed(ASIOTime& time) noexcept {
    time.timeInfo.flags |= kSpeedValid;
    time.timeInfo.speed = 0.5;
}

void ChangedClockSource(ASIOTime& time) noexcept {
    time.timeInfo.flags |= kClockSourceChanged;
}

int TestTimeInfoValidation() {
    using Mutation = void (*)(ASIOTime&) noexcept;
    constexpr std::array<Mutation, 7> mutations{
        &MissingFlags,
        &NegativeSamples,
        &NegativeTimestamp,
        &ChangedRate,
        &WrongRate,
        &WrongSpeed,
        &ChangedClockSource,
    };

    int failures{};
    for (const auto mutate : mutations) {
        FakeRenderer renderer;
        FakeActionsState actions;
        auto runtime = PrepareRuntime(renderer, actions);
        if (runtime == nullptr) {
            ++failures;
            continue;
        }
        failures += Expect(Install(*runtime), "validation runtime installs");
        auto time = MakeTime(0, 1'000'000);
        mutate(time);
        AsioCallbackRuntime::Callbacks()->bufferSwitchTimeInfo(
            &time,
            0,
            ASIOTrue);
        failures += Expect(
            renderer.render_count.load() == 0 &&
                renderer.clear_counts[0].load() == 1 &&
                runtime->Snapshot().first_fault ==
                    AsioFailureStage::runtime_clock,
            "invalid time info is silenced and latches clock fault");
        Shutdown(*runtime);
    }

    FakeRenderer null_renderer;
    FakeActionsState null_actions;
    auto null_runtime = PrepareRuntime(null_renderer, null_actions);
    if (null_runtime == nullptr) {
        return failures + 1;
    }
    failures += Expect(Install(*null_runtime), "null-time runtime installs");
    AsioCallbackRuntime::Callbacks()->bufferSwitchTimeInfo(
        nullptr,
        0,
        ASIOTrue);
    failures += Expect(
        null_renderer.render_count.load() == 0 &&
            null_renderer.clear_counts[0].load() == 1,
        "null ASIOTime is rejected and silenced");
    Shutdown(*null_runtime);
    return failures;
}

int TestLegacyValidation() {
    int failures{};
    for (int failure_kind = 0; failure_kind < 3; ++failure_kind) {
        FakeRenderer renderer;
        FakeActionsState actions;
        SetSamples(actions.legacy_samples, 0);
        SetTimestamp(actions.legacy_timestamp, 1'000'000);
        if (failure_kind == 0) {
            actions.legacy_result = ASE_HWMalfunction;
        } else if (failure_kind == 1) {
            MakeSamplesNegative(actions.legacy_samples);
        } else {
            MakeTimestampNegative(actions.legacy_timestamp);
        }
        auto runtime = PrepareRuntime(renderer, actions);
        if (runtime == nullptr) {
            ++failures;
            continue;
        }
        failures += Expect(Install(*runtime), "legacy validation installs");
        AsioCallbackRuntime::Callbacks()->bufferSwitch(0, ASIOTrue);
        failures += Expect(
            renderer.render_count.load() == 0 &&
                renderer.clear_counts[0].load() == 1 &&
                runtime->Snapshot().first_fault != AsioFailureStage::none,
            "failed or negative legacy position pair is silenced");
        Shutdown(*runtime);
    }
    return failures;
}

int TestMessagesAndRateNotification() {
    FakeRenderer renderer;
    FakeActionsState actions;
    auto runtime = PrepareRuntime(renderer, actions);
    if (runtime == nullptr) {
        return 1;
    }
    int failures = Expect(Install(*runtime), "message runtime installs");
    auto* callbacks = AsioCallbackRuntime::Callbacks();

    constexpr std::array<long, 8> supported{
        kAsioSelectorSupported,
        kAsioEngineVersion,
        kAsioResetRequest,
        kAsioBufferSizeChange,
        kAsioResyncRequest,
        kAsioLatenciesChanged,
        kAsioSupportsTimeInfo,
        kAsioOverload,
    };
    for (const auto selector : supported) {
        failures += Expect(
            callbacks->asioMessage(
                kAsioSelectorSupported,
                selector,
                nullptr,
                nullptr) == 1,
            "handled ASIO selector reports support");
    }
    failures += Expect(
        callbacks->asioMessage(
            kAsioSelectorSupported,
            kAsioSupportsTimeCode,
            nullptr,
            nullptr) == 0 &&
            callbacks->asioMessage(
                kAsioSelectorSupported,
                kAsioMMCCommand,
                nullptr,
                nullptr) == 0,
        "unhandled selectors do not report support");
    failures += Expect(
        callbacks->asioMessage(kAsioEngineVersion, 0, nullptr, nullptr) == 2 &&
            callbacks->asioMessage(
                kAsioSupportsTimeInfo,
                0,
                nullptr,
                nullptr) == 1 &&
            callbacks->asioMessage(
                kAsioSupportsTimeCode,
                0,
                nullptr,
                nullptr) == 0,
        "host reports ASIO 2 time-info support without time code");

    failures += Expect(
        callbacks->asioMessage(kAsioResetRequest, 0, nullptr, nullptr) == 1,
        "reset request is accepted for controlled restart");
    failures += Expect(
        callbacks->asioMessage(kAsioResyncRequest, 0, nullptr, nullptr) == 1,
        "resync request is accepted for controlled restart");
    failures += Expect(
        callbacks->asioMessage(kAsioLatenciesChanged, 0, nullptr, nullptr) == 1,
        "latency change is accepted for controlled restart");
    failures += Expect(
        callbacks->asioMessage(kAsioBufferSizeChange, 256, nullptr, nullptr) ==
            0,
        "in-place buffer resize is rejected");
    failures += Expect(
        callbacks->asioMessage(kAsioOverload, 0, nullptr, nullptr) == 1,
        "overload notification is handled");
    callbacks->sampleRateDidChange(48'000.0);

    const auto snapshot = runtime->Snapshot();
    failures += Expect(
        snapshot.reset_requests == 1 &&
            snapshot.resync_requests == 1 &&
            snapshot.latency_change_requests == 1 &&
            snapshot.buffer_size_change_requests == 1 &&
            snapshot.overload_messages == 1 &&
            snapshot.sample_rate_change_requests == 1 &&
            snapshot.last_reported_sample_rate == 48'000.0,
        "all runtime notifications retain typed counters and reported rate");
    failures += Expect(
        snapshot.first_fault == AsioFailureStage::callback &&
            renderer.fault_count.load() == 1,
        "first runtime fault is preserved while later counters advance");
    Shutdown(*runtime);
    return failures;
}

int TestInstallCollisionAndPreparationFailure() {
    FakeRenderer first_renderer;
    FakeRenderer second_renderer;
    FakeActionsState first_actions;
    FakeActionsState second_actions;
    auto first = PrepareRuntime(first_renderer, first_actions);
    auto second = PrepareRuntime(second_renderer, second_actions);
    if (first == nullptr || second == nullptr) {
        return 1;
    }

    int failures = Expect(Install(*first), "first global router installs");
    const auto collision = second->Install();
    failures += Expect(
        !collision.has_value() &&
            collision.error().stage == AsioFailureStage::callback_prepare,
        "second active ASIO session is rejected");
    auto time = MakeTime(0, 1'000'000);
    AsioCallbackRuntime::Callbacks()->bufferSwitchTimeInfo(
        &time,
        0,
        ASIOTrue);
    failures += Expect(
        first_renderer.render_count.load() == 1 &&
            second_renderer.render_count.load() == 0,
        "collision cannot steal the active callback router");
    Shutdown(*first);
    Shutdown(*second);

    FakeRenderer failed_renderer;
    FakeActionsState failed_actions;
    failed_actions.promotion_succeeds = false;
    auto failed = AsioCallbackRuntime::Prepare(
        failed_renderer,
        LegacyActions(failed_actions),
        RuntimeActions(failed_actions));
    failures += Expect(
        !failed.has_value() &&
            failed.error().stage == AsioFailureStage::callback_prepare,
        "MMCSS promotion failure rejects runtime preparation");

    FakeRenderer timer_renderer;
    FakeActionsState timer_actions;
    timer_actions.frequency_succeeds = false;
    auto bad_timer = AsioCallbackRuntime::Prepare(
        timer_renderer,
        LegacyActions(timer_actions),
        RuntimeActions(timer_actions));
    failures += Expect(
        !bad_timer.has_value() &&
            bad_timer.error().stage == AsioFailureStage::callback_prepare,
        "performance-counter setup failure rejects preparation");
    return failures;
}

} // namespace

int main() {
    int failures{};
    failures += TestCallbacksAreInertOutsideInstallation();
    failures += TestInlineTimeInfoAndLegacyCallbacks();
    failures += TestDeferredRequestIsBounded();
    failures += TestOverlappingInlineAndDeferredCallbacksLoseClaim();
    failures += TestBufferAlternationAndStopping();
    failures += TestTimeInfoValidation();
    failures += TestLegacyValidation();
    failures += TestMessagesAndRateNotification();
    failures += TestInstallCollisionAndPreparationFailure();
    return failures == 0 ? 0 : 1;
}
