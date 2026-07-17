#include "WasapiAudioPatch.h"
#include "WasapiAudioPatchInternal.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <dsound.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using gc::audio::AudioHookFailure;
using gc::audio::AudioHookStage;
using gc::audio::AudioStartupFailure;
using gc::audio::IAudioEngineServices;
using gc::audio::IAudioStartupFailureReporter;
using gc::audio::IExclusiveEngineFactory;

static_assert(std::is_nothrow_move_constructible_v<AudioStartupFailure>);
static_assert(std::is_nothrow_move_assignable_v<AudioStartupFailure>);

constexpr gc::audio::AudioMinHookApi kDefaultMinHookApi;
static_assert(kDefaultMinHookApi.initialize == nullptr);
static_assert(kDefaultMinHookApi.create == nullptr);
static_assert(kDefaultMinHookApi.queue_enable == nullptr);
static_assert(kDefaultMinHookApi.apply == nullptr);
static_assert(kDefaultMinHookApi.disable == nullptr);
static_assert(kDefaultMinHookApi.remove == nullptr);

constexpr gc::audio::detail::AudioResolverApi kDefaultResolverApi;
static_assert(kDefaultResolverApi.get_module_handle == nullptr);
static_assert(kDefaultResolverApi.get_proc_address == nullptr);

struct FakeState {
    HMODULE module{reinterpret_cast<HMODULE>(0x1000)};
    FARPROC export_address{reinterpret_cast<FARPROC>(0x2000)};
    MH_STATUS initialize_status{MH_OK};
    MH_STATUS create_status{MH_OK};
    MH_STATUS queue_status{MH_OK};
    MH_STATUS apply_status{MH_OK};
    MH_STATUS disable_status{MH_OK};
    MH_STATUS remove_status{MH_OK};
    int module_calls{0};
    int export_calls{0};
    int initialize_calls{0};
    int apply_calls{0};
    int engine_factory_calls{0};
    std::wstring module_name;
    std::string export_name;
    std::vector<LPVOID> created;
    std::vector<LPVOID> queued;
    std::vector<LPVOID> disabled;
    std::vector<LPVOID> removed;
    std::vector<std::string> calls;
    LPVOID detour{};
    LPVOID* original_storage{};
};

FakeState* g_fake{};

HMODULE WINAPI fake_get_module_handle(LPCWSTR module_name) {
    g_fake->calls.emplace_back("resolve-module");
    ++g_fake->module_calls;
    g_fake->module_name = module_name == nullptr ? L"" : module_name;
    return g_fake->module;
}

FARPROC WINAPI fake_get_proc_address(HMODULE module, LPCSTR export_name) {
    g_fake->calls.emplace_back("resolve-export");
    ++g_fake->export_calls;
    if (module != g_fake->module) {
        return nullptr;
    }
    g_fake->export_name = export_name == nullptr ? "" : export_name;
    return g_fake->export_address;
}

MH_STATUS WINAPI fake_initialize() {
    g_fake->calls.emplace_back("initialize");
    ++g_fake->initialize_calls;
    return g_fake->initialize_status;
}

MH_STATUS WINAPI fake_create(LPVOID target, LPVOID detour, LPVOID* original) {
    g_fake->calls.emplace_back("create");
    g_fake->created.push_back(target);
    g_fake->detour = detour;
    g_fake->original_storage = original;
    if (g_fake->create_status == MH_OK && original != nullptr) {
        *original = reinterpret_cast<LPVOID>(0x3000);
    }
    return g_fake->create_status;
}

MH_STATUS WINAPI fake_queue_enable(LPVOID target) {
    g_fake->calls.emplace_back("queue");
    g_fake->queued.push_back(target);
    return g_fake->queue_status;
}

MH_STATUS WINAPI fake_apply_queued() {
    g_fake->calls.emplace_back("apply");
    ++g_fake->apply_calls;
    return g_fake->apply_status;
}

MH_STATUS WINAPI fake_disable(LPVOID target) {
    g_fake->calls.emplace_back("disable");
    g_fake->disabled.push_back(target);
    return g_fake->disable_status;
}

MH_STATUS WINAPI fake_remove(LPVOID target) {
    g_fake->calls.emplace_back("remove");
    g_fake->removed.push_back(target);
    return g_fake->remove_status;
}

gc::audio::AudioMinHookApi fake_minhook_api() {
    return {
        fake_initialize,
        fake_create,
        fake_queue_enable,
        fake_apply_queued,
        fake_disable,
        fake_remove,
    };
}

gc::audio::detail::AudioResolverApi fake_resolver_api() {
    return {fake_get_module_handle, fake_get_proc_address};
}

bool install(
    bool enabled,
    FakeState& state,
    AudioHookFailure* failure) {
    g_fake = &state;
    return gc::audio::detail::InstallWasapiAudioHookWithResolver(
        enabled,
        fake_minhook_api(),
        fake_resolver_api(),
        failure);
}

bool install_with_apis(
    bool enabled,
    FakeState& state,
    gc::audio::AudioMinHookApi minhook,
    gc::audio::detail::AudioResolverApi resolver,
    AudioHookFailure* failure) {
    g_fake = &state;
    return gc::audio::detail::InstallWasapiAudioHookWithResolver(
        enabled,
        minhook,
        resolver,
        failure);
}

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

struct DiagnosticState {
    std::vector<std::string> info;
    std::vector<std::string> errors;
    std::vector<std::string> messages;
    std::vector<DWORD> termination_codes;
    int fail_fast_calls{};
};

DiagnosticState* g_diagnostics{};

void fake_log_info(const char* text) {
    g_diagnostics->info.emplace_back(text == nullptr ? "" : text);
}

void fake_log_error(const char* text) {
    g_diagnostics->errors.emplace_back(text == nullptr ? "" : text);
}

void fake_show_error(const char* text) {
    g_diagnostics->messages.emplace_back(text == nullptr ? "" : text);
}

void fake_terminate_process(DWORD exit_code) {
    g_diagnostics->termination_codes.push_back(exit_code);
}

void fake_fail_fast() {
    ++g_diagnostics->fail_fast_calls;
}

struct FailFastEscape {};

void fake_throwing_fail_fast() {
    ++g_diagnostics->fail_fast_calls;
    throw FailFastEscape{};
}

gc::audio::detail::AudioPatchPlatformActions fake_platform_actions() {
    return {
        fake_log_info,
        fake_log_error,
        fake_show_error,
        fake_terminate_process,
        fake_fail_fast,
    };
}

bool contains(std::string_view text, std::string_view fragment) {
    return text.find(fragment) != std::string_view::npos;
}

bool only_target(const std::vector<LPVOID>& values, LPVOID target) {
    return values == std::vector<LPVOID>{target};
}

bool never_all_hooks(const FakeState& state) {
    LPVOID all_hooks = MH_ALL_HOOKS;
    const auto contains_all_hooks = [all_hooks](
                                        const std::vector<LPVOID>& values) {
        return std::find(values.begin(), values.end(), all_hooks) !=
            values.end();
    };
    return !contains_all_hooks(state.created) &&
        !contains_all_hooks(state.queued) &&
        !contains_all_hooks(state.disabled) &&
        !contains_all_hooks(state.removed);
}

int expect_failure(
    const AudioHookFailure& actual,
    AudioHookStage stage,
    MH_STATUS status,
    DWORD win32_error,
    LPVOID target,
    const char* name) {
    int failures = 0;
    failures += expect(actual.stage == stage, name);
    failures += expect(actual.status == status, name);
    failures += expect(actual.win32_error == win32_error, name);
    failures += expect(actual.target == target, name);
    return failures;
}

int expect_rollback(
    const AudioHookFailure& actual,
    bool attempted,
    MH_STATUS disable_status,
    MH_STATUS remove_status,
    bool complete,
    const char* name) {
    int failures = 0;
    failures += expect(actual.rollback_attempted == attempted, name);
    failures += expect(
        actual.rollback_disable_status == disable_status,
        name);
    failures += expect(actual.rollback_remove_status == remove_status, name);
    failures += expect(actual.rollback_complete == complete, name);
    return failures;
}

int expect_no_calls(const FakeState& state, const char* name) {
    return expect(
        state.calls.empty() && state.module_calls == 0 &&
            state.export_calls == 0 && state.initialize_calls == 0 &&
            state.created.empty() && state.queued.empty() &&
            state.apply_calls == 0 && state.disabled.empty() &&
            state.removed.empty(),
        name);
}

int exercise_rollback_statuses(
    bool queue_origin,
    MH_STATUS disable_status,
    MH_STATUS remove_status,
    bool expected_complete,
    const char* name) {
    const auto target = reinterpret_cast<LPVOID>(0x2000);
    FakeState state{};
    state.queue_status =
        queue_origin ? MH_ERROR_MEMORY_PROTECT : MH_OK;
    state.apply_status =
        queue_origin ? MH_OK : MH_ERROR_MEMORY_PROTECT;
    state.disable_status = disable_status;
    state.remove_status = remove_status;

    AudioHookFailure failure{};
    int failures = 0;
    failures += expect(!install(true, state, &failure), name);
    failures += expect_failure(
        failure,
        queue_origin ? AudioHookStage::QueueEnable
                     : AudioHookStage::ApplyQueued,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_SUCCESS,
        target,
        name);
    failures += expect_rollback(
        failure,
        true,
        disable_status,
        remove_status,
        expected_complete,
        name);
    failures += expect(
        only_target(state.disabled, target) &&
            only_target(state.removed, target),
        name);
    failures += expect(
        state.calls.size() >= 2 &&
            state.calls[state.calls.size() - 2] == "disable" &&
            state.calls.back() == "remove",
        name);
    return failures;
}

int expect_invalid_api_rejected(
    gc::audio::AudioMinHookApi minhook,
    gc::audio::detail::AudioResolverApi resolver,
    const char* name) {
    FakeState state{};
    AudioHookFailure failure{};
    int failures = 0;
    failures += expect(
        !install_with_apis(true, state, minhook, resolver, &failure),
        name);
    failures += expect_failure(
        failure,
        AudioHookStage::ValidateApi,
        MH_UNKNOWN,
        ERROR_INVALID_PARAMETER,
        nullptr,
        name);
    failures += expect_rollback(
        failure,
        false,
        MH_OK,
        MH_OK,
        true,
        name);
    failures += expect_no_calls(state, name);
    return failures;
}

class FakeEngineServices final : public IAudioEngineServices {
public:
    std::unique_ptr<gc::audio::MixerVoice> CreateVoice(
        const gc::audio::NormalizedSourceFormat&,
        std::shared_ptr<gc::audio::AudioSnapshot>,
        std::shared_ptr<gc::audio::AudioCursorTimeline>,
        gc::audio::VoiceUsage,
        ma_result*) noexcept override {
        return nullptr;
    }

    std::optional<std::uint64_t> CurrentOutputFrame() noexcept override {
        return 0;
    }

    std::uint32_t endpoint_buffer_frames() const noexcept override {
        return 0;
    }

    std::uint32_t output_sample_rate() const noexcept override {
        return gc::audio::kGamePrimarySampleRate;
    }

    void CountPendingCursorQuery() noexcept override {}

    void CountUnmappedCursorFailure() noexcept override {}
};

class FakeEngineFactory final : public IExclusiveEngineFactory {
public:
    IAudioEngineServices* GetOrCreate(
        const AudioStartupFailure** failure) noexcept override {
        ++calls;
        if (failure != nullptr) {
            *failure = engine == nullptr ? &failure_to_return : nullptr;
        }
        return engine;
    }

    IAudioEngineServices* engine{};
    AudioStartupFailure failure_to_return{};
    int calls{};
};

class FakeEngineStartup final
    : public gc::audio::detail::IExclusiveEngineStartup {
public:
    IAudioEngineServices* Start(
        AudioStartupFailure* output) noexcept override {
        calls.fetch_add(1, std::memory_order_relaxed);
        {
            std::unique_lock lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [this] {
                return !block_until_released || released;
            });
        }
        if (output != nullptr) {
            *output = std::move(failure);
        }
        return engine;
    }

    void WaitUntilEntered() {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this] { return entered; });
    }

    void Release() {
        {
            std::lock_guard lock(mutex);
            released = true;
        }
        condition.notify_all();
    }

    IAudioEngineServices* engine{};
    AudioStartupFailure failure{};
    std::atomic_int calls{};
    bool block_until_released{};

private:
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{};
    bool released{};
};

class FakeFailureReporter final : public IAudioStartupFailureReporter {
public:
    void FatalStartupFailure(
        const AudioStartupFailure& failure) noexcept override {
        ++calls;
        reported = &failure;
    }

    int calls{};
    const AudioStartupFailure* reported{};
};

std::atomic_int g_saved_original_calls{};

HRESULT WINAPI fake_saved_original(
    LPCGUID,
    LPDIRECTSOUND8*,
    LPUNKNOWN) {
    g_saved_original_calls.fetch_add(1, std::memory_order_relaxed);
    return DS_OK;
}

AudioStartupFailure exact_startup_failure() {
    AudioStartupFailure failure{};
    failure.failure = {
        gc::audio::AudioFailureStage::GetActualBufferSize,
        HRESULT_FROM_WIN32(ERROR_BAD_FORMAT),
    };
    failure.attempted.endpoint_name = L"Fake endpoint";
    failure.attempted.endpoint_id = L"fake-endpoint-id";
    failure.attempted.minimum_period = 20'000;
    failure.attempted.configured_duration = 100'000;
    failure.attempted.requested_duration = 20'000;
    failure.attempted.actual_buffer_frames = 96;
    return failure;
}

bool same_failure(
    const AudioStartupFailure& left,
    const AudioStartupFailure& right) {
    return left.failure.stage == right.failure.stage &&
        left.failure.result == right.failure.result &&
        left.attempted.endpoint_name == right.attempted.endpoint_name &&
        left.attempted.endpoint_id == right.attempted.endpoint_id &&
        left.attempted.default_period == right.attempted.default_period &&
        left.attempted.minimum_period == right.attempted.minimum_period &&
        left.attempted.configured_duration ==
            right.attempted.configured_duration &&
        left.attempted.requested_duration ==
            right.attempted.requested_duration &&
        left.attempted.stream_latency == right.attempted.stream_latency &&
        left.attempted.stream_latency_result ==
            right.attempted.stream_latency_result &&
        left.attempted.stream_latency_available ==
            right.attempted.stream_latency_available &&
        left.attempted.actual_buffer_frames ==
            right.attempted.actual_buffer_frames &&
        left.attempted.clock_frequency ==
            right.attempted.clock_frequency &&
        left.attempted.alignment_retry ==
            right.attempted.alignment_retry;
}

int test_detour_validation_and_success() {
    int failures = 0;
    FakeEngineFactory validation_factory;
    FakeFailureReporter reporter;
    g_saved_original_calls.store(0, std::memory_order_relaxed);

    failures += expect(
        gc::audio::detail::InvokeDirectSoundCreate8Detour(
            nullptr,
            nullptr,
            nullptr,
            validation_factory,
            reporter,
            fake_saved_original) == DSERR_INVALIDPARAM,
        "direct detour rejects null output pointer");

    GUID unexpected_device{};
    auto* output = reinterpret_cast<LPDIRECTSOUND8>(0x1);
    failures += expect(
        gc::audio::detail::InvokeDirectSoundCreate8Detour(
            &unexpected_device,
            &output,
            nullptr,
            validation_factory,
            reporter,
            fake_saved_original) == DSERR_NODRIVER,
        "direct detour rejects nondefault device");
    failures += expect(
        output == nullptr,
        "direct detour nulls output before GUID validation");

    output = reinterpret_cast<LPDIRECTSOUND8>(0x1);
    failures += expect(
        gc::audio::detail::InvokeDirectSoundCreate8Detour(
            nullptr,
            &output,
            reinterpret_cast<LPUNKNOWN>(0x1),
            validation_factory,
            reporter,
            fake_saved_original) == DSERR_NOAGGREGATION,
        "direct detour rejects aggregation");
    failures += expect(
        output == nullptr,
        "direct detour nulls output before aggregation validation");
    failures += expect(
        validation_factory.calls == 0 && reporter.calls == 0 &&
            g_saved_original_calls.load(std::memory_order_relaxed) == 0,
        "validation paths perform no factory reporter or original work");

    FakeEngineServices engine;
    FakeEngineStartup startup;
    startup.engine = &engine;
    gc::audio::detail::CachedExclusiveEngineFactory factory(startup);

    LPDIRECTSOUND8 first{};
    LPDIRECTSOUND8 second{};
    failures += expect(
        gc::audio::detail::InvokeDirectSoundCreate8Detour(
            nullptr,
            &first,
            nullptr,
            factory,
            reporter,
            fake_saved_original) == DS_OK,
        "first valid detour creates DirectSound device");
    failures += expect(
        gc::audio::detail::InvokeDirectSoundCreate8Detour(
            nullptr,
            &second,
            nullptr,
            factory,
            reporter,
            fake_saved_original) == DS_OK,
        "second valid detour creates DirectSound device");
    failures += expect(
        first != nullptr && second != nullptr && first != second,
        "valid detours return independent device facades");
    failures += expect(
        startup.calls.load(std::memory_order_relaxed) == 1,
        "two valid detours initialize engine once");
    const AudioStartupFailure sentinel_failure{};
    const AudioStartupFailure* success_failure = &sentinel_failure;
    failures += expect(
        factory.GetOrCreate(&success_failure) == &engine &&
            success_failure == nullptr,
        "successful cached lookup publishes null failure");
    failures += expect(
        reporter.calls == 0,
        "successful detours do not report startup failure");
    failures += expect(
        g_saved_original_calls.load(std::memory_order_relaxed) == 0,
        "successful detours never call saved original");
    if (first != nullptr) {
        first->Release();
    }
    if (second != nullptr) {
        second->Release();
    }
    return failures;
}

int test_failure_reporting_and_cache() {
    int failures = 0;
    FakeEngineStartup startup;
    startup.failure = exact_startup_failure();
    const auto expected_failure = exact_startup_failure();
    gc::audio::detail::CachedExclusiveEngineFactory factory(startup);
    FakeFailureReporter reporter;
    g_saved_original_calls.store(0, std::memory_order_relaxed);

    auto* output = reinterpret_cast<LPDIRECTSOUND8>(0x1);
    failures += expect(
        gc::audio::detail::InvokeDirectSoundCreate8Detour(
            nullptr,
            &output,
            nullptr,
            factory,
            reporter,
            fake_saved_original) == DSERR_NODRIVER,
        "engine startup failure returns no driver");
    failures += expect(
        output == nullptr,
        "engine startup failure leaves output null");
    failures += expect(
        reporter.calls == 1 && reporter.reported != nullptr &&
            same_failure(*reporter.reported, expected_failure),
        "engine startup failure reports exact failure once");
    failures += expect(
        g_saved_original_calls.load(std::memory_order_relaxed) == 0,
        "failed detour never calls saved original");

    const AudioStartupFailure* cached_failure{};
    failures += expect(
        factory.GetOrCreate(&cached_failure) == nullptr,
        "cached failure returns no engine");
    failures += expect(
        startup.calls.load(std::memory_order_relaxed) == 1,
        "cached failure is not retried");
    failures += expect(
        cached_failure != nullptr &&
            same_failure(*cached_failure, expected_failure),
        "cached failure preserves exact startup diagnostics");
    failures += expect(
        cached_failure == reporter.reported,
        "cached failure replays one stable immutable value");
    failures += expect(
        reporter.calls == 1,
        "direct cached lookup does not report a second fatal failure");
    return failures;
}

int test_concurrent_first_callers_share_initialization() {
    int failures = 0;
    FakeEngineServices engine;
    FakeEngineStartup startup;
    startup.engine = &engine;
    startup.block_until_released = true;
    gc::audio::detail::CachedExclusiveEngineFactory factory(startup);
    std::atomic_bool begin{};
    IAudioEngineServices* first{};
    IAudioEngineServices* second{};

    auto call_factory = [&](IAudioEngineServices** result) {
        while (!begin.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        const AudioStartupFailure* failure{};
        *result = factory.GetOrCreate(&failure);
    };
    std::thread first_caller(call_factory, &first);
    std::thread second_caller(call_factory, &second);
    begin.store(true, std::memory_order_release);
    startup.WaitUntilEntered();
    for (int attempt = 0; attempt != 100; ++attempt) {
        std::this_thread::yield();
    }
    failures += expect(
        startup.calls.load(std::memory_order_relaxed) == 1,
        "concurrent first callers allow one initializer");
    startup.Release();
    first_caller.join();
    second_caller.join();
    failures += expect(
        first == &engine && second == &engine,
        "concurrent first callers receive published engine");
    failures += expect(
        startup.calls.load(std::memory_order_relaxed) == 1,
        "concurrent publication does not retry initialization");
    return failures;
}

int test_production_diagnostics_use_injected_platform_actions() {
    int failures = 0;
    DiagnosticState diagnostics;
    g_diagnostics = &diagnostics;
    const auto actions = fake_platform_actions();

    gc::audio::EndpointInitialization initialization{};
    initialization.endpoint_name = L"Fake Pro Audio Endpoint";
    initialization.endpoint_id = L"endpoint-id-123";
    initialization.default_period = 100'000;
    initialization.minimum_period = 20'000;
    initialization.configured_duration = 100'000;
    initialization.requested_duration = 30'000;
    initialization.stream_latency = 50'000;
    initialization.stream_latency_result = S_OK;
    initialization.stream_latency_available = true;
    initialization.actual_buffer_frames = 147;
    initialization.alignment_retry = true;
    initialization.selected_format = gc::audio::MakeEndpointPcm16Format(
        gc::audio::kGamePrimarySampleRate,
        gc::audio::EndpointFormatKind::LegacyPcm);
    initialization.has_selected_format = true;
    gc::audio::detail::ReportAudioStartupSucceeded(
        initialization,
        actions);

    failures += expect(
        diagnostics.info.size() == 1,
        "startup emits one non-real-time information record");
    const std::string_view startup = diagnostics.info.empty()
        ? std::string_view{}
        : diagnostics.info.back();
    for (const auto required : {
             "requested_backend=wasapi_exclusive",
             "active_backend=wasapi_exclusive",
             "endpoint_name=\"Fake Pro Audio Endpoint\"",
             "endpoint_id=\"endpoint-id-123\"",
             "format=pcm16/44100Hz/2ch/16bit",
             "default_period_100ns=100000",
             "default_period_ms=10.000",
             "minimum_period_100ns=20000",
             "minimum_period_ms=2.000",
             "configured_duration_100ns=100000",
             "configured_duration_ms=10.000",
             "requested_duration_100ns=30000",
             "requested_duration_ms=3.000",
             "stream_latency_100ns=50000",
             "stream_latency_ms=5.000",
             "actual_buffer_frames=147",
             "actual_buffer_ms=3.333",
             "exclusive_event_driven=true",
             "alignment_retry=true",
             "mmcss_profile=\"Pro Audio\"",
             "mmcss_priority=\"Critical\"",
             "mixer_rate_hz=44100",
             "mixer_channels=2",
         }) {
        failures += expect(
            contains(startup, required),
            "startup log contains every required field");
    }

    gc::audio::AudioRuntimeCountersSnapshot counters{};
    counters.render_callbacks = 11;
    counters.late_event_wakes = 12;
    counters.silence_fallbacks = 13;
    counters.pending_cursor_queries = 14;
    counters.unmapped_cursor_failures = 15;
    counters.confirmed_gap_events = 16;
    counters.skipped_output_frames = 17;
    counters.maximum_skipped_output_frames = 18;
    counters.chronic_pacing_failures = 19;
    counters.current_submitted_lead_frames = -20;
    counters.minimum_submitted_lead_frames = -21;
    counters.endpoint_hresult_failures = 22;
    counters.mixer.native_rate_buffers = 23;
    counters.mixer.sample_format_converted_buffers = 24;
    counters.mixer.sample_rate_converted_buffers = 25;
    counters.mixer.native_gameplay_buffers = 26;
    counters.mixer.active_voices = 27;
    counters.mixer.maximum_simultaneous_voices = 28;
    gc::audio::detail::ReportAudioRuntimeSummary(counters, actions);

    failures += expect(
        diagnostics.info.size() == 2,
        "runtime summary emits one non-real-time information record");
    const std::string_view summary = diagnostics.info.size() < 2
        ? std::string_view{}
        : diagnostics.info.back();
    for (const auto required : {
             "render_callbacks=11",
             "late_event_wakes=12",
             "silence_fallbacks=13",
             "pending_cursor_queries=14",
             "unmapped_cursor_failures=15",
             "confirmed_gap_events=16",
             "skipped_output_frames=17",
             "maximum_skipped_output_frames=18",
             "chronic_pacing_failures=19",
             "current_submitted_lead_frames=-20",
             "minimum_submitted_lead_frames=-21",
             "endpoint_hresult_failures=22",
             "native_rate_buffers=23",
             "sample_format_converted_buffers=24",
             "sample_rate_converted_buffers=25",
             "native_gameplay_buffers=26",
             "active_voices=27",
             "maximum_simultaneous_voices=28",
         }) {
        failures += expect(
            contains(summary, required),
            "runtime summary contains every counter");
    }

    gc::audio::AudioFailure runtime_failure{
        gc::audio::AudioFailureStage::ReleaseRenderBuffer,
        HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE),
    };
    gc::audio::detail::ReportAudioRuntimeFailure(
        initialization,
        runtime_failure,
        counters,
        actions);

    failures += expect(
        diagnostics.errors.size() == 1 &&
            contains(diagnostics.errors.back(), "endpoint_id=\"endpoint-id-123\"") &&
            contains(diagnostics.errors.back(), "stage=ReleaseRenderBuffer") &&
            contains(diagnostics.errors.back(), "hresult=0x800710DF") &&
            contains(diagnostics.errors.back(), "format=pcm16/44100Hz/2ch/16bit") &&
            contains(diagnostics.errors.back(), "maximum_simultaneous_voices=28"),
        "runtime fatal logs endpoint stage HRESULT format and counters");
    failures += expect(
        diagnostics.messages.size() == 1 &&
            contains(
                diagnostics.messages.back(),
                "WASAPI exclusive low-latency audio failed.\n"
                "Restart the game after setting "
                "enable_wasapi_exclusive_audio = false\n"
                "to restore the original DirectSound backend."),
        "runtime fatal displays required disable-setting restart text");
    failures += expect(
        diagnostics.termination_codes ==
                std::vector<DWORD>{ERROR_DEVICE_NOT_AVAILABLE} &&
            diagnostics.fail_fast_calls == 1,
        "runtime fatal terminates then uses injected fail-fast fallback");

    return failures;
}

int test_pacing_specific_diagnostics() {
    int failures = 0;
    DiagnosticState diagnostics;
    g_diagnostics = &diagnostics;
    const auto actions = fake_platform_actions();

    gc::audio::EndpointInitialization unavailable{};
    unavailable.endpoint_id = L"latency-unavailable";
    unavailable.stream_latency_result = E_NOTIMPL;
    unavailable.selected_format = gc::audio::MakeEndpointPcm16Format(
        gc::audio::kGamePrimarySampleRate,
        gc::audio::EndpointFormatKind::LegacyPcm);
    unavailable.has_selected_format = true;
    gc::audio::detail::ReportAudioStartupSucceeded(unavailable, actions);
    failures += expect(
        diagnostics.info.size() == 1 &&
            contains(diagnostics.info.back(), "stream_latency=unavailable") &&
            contains(
                diagnostics.info.back(),
                "stream_latency_hresult=0x80004001"),
        "unsupported stream latency is explicit startup metadata");

    diagnostics = {};
    unavailable.configured_duration = 100'000;
    unavailable.minimum_period = 30'000;
    unavailable.actual_buffer_frames = 441;
    gc::audio::AudioRuntimeCountersSnapshot counters{};
    counters.confirmed_gap_events = 3;
    counters.skipped_output_frames = 882;
    counters.maximum_skipped_output_frames = 441;
    counters.chronic_pacing_failures = 1;
    gc::audio::detail::ReportAudioRuntimeFailure(
        unavailable,
        {gc::audio::AudioFailureStage::ChronicOutputGap, E_FAIL},
        counters,
        actions);
    failures += expect(
        diagnostics.errors.size() == 1 &&
            contains(diagnostics.errors.back(), "stage=ChronicOutputGap") &&
            contains(diagnostics.errors.back(), "confirmed_gap_events=3") &&
            contains(diagnostics.errors.back(), "skipped_output_frames=882") &&
            contains(
                diagnostics.errors.back(),
                "maximum_skipped_output_frames=441") &&
            diagnostics.messages.size() == 1 &&
            contains(
                diagnostics.messages.back(),
                "increase wasapi_exclusive_buffer_ms and restart"),
        "chronic pacing fatal gives buffer-specific recovery guidance");
    return failures;
}

std::unique_ptr<gc::audio::IWasapiApi> fake_null_wasapi_api() noexcept {
    return nullptr;
}

int g_start_engine_calls{};
DWORD g_start_engine_timeout{};
REFERENCE_TIME g_start_engine_configured_duration{};

std::unique_ptr<gc::audio::ExclusiveAudioEngine> fake_start_engine(
    std::unique_ptr<gc::audio::IWasapiApi>,
    std::shared_ptr<gc::audio::IAudioEngineObserver>,
    DWORD timeout,
    REFERENCE_TIME configured_duration,
    std::shared_ptr<const ma_allocation_callbacks>,
    AudioStartupFailure*) noexcept {
    ++g_start_engine_calls;
    g_start_engine_timeout = timeout;
    g_start_engine_configured_duration = configured_duration;
    return nullptr;
}

int test_null_production_api_and_startup_fatal_reporting() {
    int failures = 0;
    DiagnosticState diagnostics;
    g_diagnostics = &diagnostics;
    const auto actions = fake_platform_actions();
    g_start_engine_calls = 0;
    g_start_engine_timeout = 0;
    g_start_engine_configured_duration = 0;

    AudioStartupFailure forwarded_failure{};
    auto engine = gc::audio::detail::StartProductionExclusiveAudioEngine(
        &gc::audio::CreateProductionWasapiApi,
        fake_start_engine,
        100'000,
        actions,
        {},
        &forwarded_failure);
    failures += expect(
        engine == nullptr && g_start_engine_calls == 1 &&
            g_start_engine_timeout == 10'000 &&
            g_start_engine_configured_duration == 100'000,
        "production startup forwards fixed configured duration");
    failures += expect(
        diagnostics.info.size() == 1 &&
            contains(
                diagnostics.info.back(),
                "stage=production_engine_start") &&
            contains(diagnostics.info.back(), "configured_buffer_ms=10") &&
            contains(
                diagnostics.info.back(),
                "configured_duration_100ns=100000"),
        "production startup logs the duration immediately before StartAndWait");

    g_start_engine_calls = 0;

    AudioStartupFailure allocation_failure{};
    engine = gc::audio::detail::StartProductionExclusiveAudioEngine(
        fake_null_wasapi_api,
        fake_start_engine,
        100'000,
        actions,
        {},
        &allocation_failure);
    failures += expect(
        engine == nullptr && g_start_engine_calls == 0,
        "null production WASAPI API fails before StartAndWait");
    failures += expect(
        allocation_failure.failure.stage ==
                gc::audio::AudioFailureStage::InitializeMixer &&
            allocation_failure.failure.result == E_OUTOFMEMORY,
        "null production WASAPI API publishes InitializeMixer E_OUTOFMEMORY");

    const auto startup_failure = exact_startup_failure();
    gc::audio::detail::ReportAudioStartupFailure(startup_failure, actions);
    failures += expect(
        diagnostics.errors.size() == 1 &&
            contains(diagnostics.errors.back(), "endpoint_id=\"fake-endpoint-id\"") &&
            contains(diagnostics.errors.back(), "stage=GetActualBufferSize") &&
            contains(diagnostics.errors.back(), "hresult=0x8007000B") &&
            contains(diagnostics.errors.back(), "format=pcm16/44100Hz/2ch/16bit"),
        "startup fatal logs endpoint stage HRESULT and exact format");
    failures += expect(
        diagnostics.messages.size() == 1 &&
            contains(
                diagnostics.messages.back(),
                "enable_wasapi_exclusive_audio = false"),
        "startup fatal displays required disable setting");
    failures += expect(
        diagnostics.termination_codes ==
                std::vector<DWORD>{ERROR_DEVICE_NOT_AVAILABLE} &&
            diagnostics.fail_fast_calls == 1,
        "startup fatal terminates then uses injected fail-fast fallback");
    return failures;
}

int test_config_gate_and_attach_failure_policy() {
    int failures = 0;
    DiagnosticState diagnostics;
    g_diagnostics = &diagnostics;

    FakeState disabled;
    g_fake = &disabled;
    failures += expect(
        gc::audio::detail::WasapiAudioPatchInitWithDependencies(
            false,
            10,
            {
                fake_minhook_api(),
                fake_resolver_api(),
                fake_platform_actions(),
            }),
        "disabled config returns success");
    failures += expect_no_calls(
        disabled,
        "disabled config performs zero resolution MinHook or engine work");
    failures += expect(
        diagnostics.info.size() == 1 &&
            contains(diagnostics.info.back(), "requested_backend=directsound") &&
            contains(diagnostics.info.back(), "active_backend=directsound") &&
            contains(diagnostics.info.back(), "hook_installed=false") &&
            contains(diagnostics.info.back(), "configured_buffer_ms=10") &&
            contains(
                diagnostics.info.back(),
                "configured_duration_100ns=100000") &&
            diagnostics.errors.empty() && diagnostics.messages.empty() &&
            diagnostics.termination_codes.empty(),
        "disabled config logs original DirectSound backend only");

    diagnostics = {};
    FakeState enabled;
    g_fake = &enabled;
    failures += expect(
        gc::audio::detail::WasapiAudioPatchInitWithDependencies(
            true,
            10,
            {
                fake_minhook_api(),
                fake_resolver_api(),
                fake_platform_actions(),
            }),
        "enabled config returns success");
    failures += expect(
        diagnostics.info.size() == 1 &&
            contains(
                diagnostics.info.back(),
                "requested_backend=wasapi_exclusive") &&
            contains(
                diagnostics.info.back(),
                "active_backend=wasapi_exclusive") &&
            contains(diagnostics.info.back(), "hook_installed=true") &&
            contains(diagnostics.info.back(), "configured_buffer_ms=10") &&
            contains(
                diagnostics.info.back(),
                "configured_duration_100ns=100000"),
        "enabled config logs the parsed buffer duration before the detour runs");

    diagnostics = {};
    FakeState clean_failure;
    clean_failure.module = nullptr;
    g_fake = &clean_failure;
    failures += expect(
        !gc::audio::detail::WasapiAudioPatchInitWithDependencies(
            true,
            10,
            {
                fake_minhook_api(),
                fake_resolver_api(),
                fake_platform_actions(),
            }),
        "clean hook install failure may fail attach normally");
    failures += expect(
        diagnostics.errors.size() == 1 &&
            contains(diagnostics.errors.back(), "stage=ResolveModule") &&
            contains(diagnostics.errors.back(), "status=0") &&
            contains(diagnostics.errors.back(), "win32_error=126") &&
            contains(diagnostics.errors.back(), "target=0x00000000") &&
            contains(diagnostics.errors.back(), "rollback_attempted=false") &&
            contains(diagnostics.errors.back(), "rollback_disable_status=0") &&
            contains(diagnostics.errors.back(), "rollback_remove_status=0") &&
            contains(diagnostics.errors.back(), "rollback_complete=true"),
        "hook failure logs every forward and rollback field");
    failures += expect(
        diagnostics.messages.size() == 1 &&
            contains(
                diagnostics.messages.back(),
                "enable_wasapi_exclusive_audio = false") &&
            diagnostics.termination_codes.empty(),
        "clean hook failure is actionable and does not terminate");

    diagnostics = {};
    FakeState incomplete_failure;
    incomplete_failure.apply_status = MH_ERROR_MEMORY_PROTECT;
    incomplete_failure.remove_status = MH_ERROR_MEMORY_PROTECT;
    g_fake = &incomplete_failure;
    auto actions = fake_platform_actions();
    actions.fail_fast = fake_throwing_fail_fast;
    bool returned = false;
    bool fail_fast_escaped = false;
    try {
        returned = gc::audio::detail::WasapiAudioPatchInitWithDependencies(
            true,
            10,
            {
                fake_minhook_api(),
                fake_resolver_api(),
                actions,
            });
    } catch (const FailFastEscape&) {
        fail_fast_escaped = true;
    }
    failures += expect(
        !returned && fail_fast_escaped && diagnostics.fail_fast_calls == 1,
        "incomplete rollback never returns normal attach failure");
    failures += expect(
        diagnostics.termination_codes ==
            std::vector<DWORD>{ERROR_DLL_INIT_FAILED},
        "incomplete rollback terminates with ERROR_DLL_INIT_FAILED");
    failures += expect(
        diagnostics.errors.size() == 1 &&
            contains(diagnostics.errors.back(), "stage=ApplyQueued") &&
            contains(diagnostics.errors.back(), "rollback_attempted=true") &&
            contains(diagnostics.errors.back(), "rollback_complete=false"),
        "incomplete rollback logs actionable transaction result before stop");
    failures += expect(
        incomplete_failure.engine_factory_calls == 0,
        "attach gate performs no engine work under loader lock");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    const auto target = reinterpret_cast<LPVOID>(0x2000);

    FakeState disabled_null_failure{};
    failures += expect(
        install_with_apis(
            false,
            disabled_null_failure,
            {},
            {},
            nullptr),
        "disabled mode accepts null failure output");
    failures += expect_no_calls(
        disabled_null_failure,
        "disabled null failure performs zero validation and calls");

    FakeState enabled_null_failure{};
    failures += expect(
        !install_with_apis(
            true,
            enabled_null_failure,
            fake_minhook_api(),
            fake_resolver_api(),
            nullptr),
        "enabled mode rejects null failure output");
    failures += expect_no_calls(
        enabled_null_failure,
        "enabled null failure performs zero validation and calls");

    FakeState disabled_incomplete{};
    AudioHookFailure disabled_incomplete_failure{
        AudioHookStage::ApplyQueued,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_ACCESS_DENIED,
        target,
    };
    failures += expect(
        install_with_apis(
            false,
            disabled_incomplete,
            {},
            {},
            &disabled_incomplete_failure),
        "disabled mode bypasses incomplete table validation");
    failures += expect_no_calls(
        disabled_incomplete,
        "disabled incomplete tables perform zero calls");
    failures += expect_failure(
        disabled_incomplete_failure,
        AudioHookStage::None,
        MH_OK,
        ERROR_SUCCESS,
        nullptr,
        "disabled incomplete tables clear failure");
    failures += expect_rollback(
        disabled_incomplete_failure,
        false,
        MH_OK,
        MH_OK,
        true,
        "disabled incomplete tables need no rollback");

    failures += expect_invalid_api_rejected(
        fake_minhook_api(),
        {},
        "empty resolver table rejected before calls");

    auto missing_initialize = fake_minhook_api();
    missing_initialize.initialize = nullptr;
    failures += expect_invalid_api_rejected(
        missing_initialize,
        fake_resolver_api(),
        "missing initialize rejected before calls");

    auto missing_create = fake_minhook_api();
    missing_create.create = nullptr;
    failures += expect_invalid_api_rejected(
        missing_create,
        fake_resolver_api(),
        "missing create rejected before calls");

    auto missing_queue = fake_minhook_api();
    missing_queue.queue_enable = nullptr;
    failures += expect_invalid_api_rejected(
        missing_queue,
        fake_resolver_api(),
        "missing queue rejected before calls");

    auto missing_apply = fake_minhook_api();
    missing_apply.apply = nullptr;
    failures += expect_invalid_api_rejected(
        missing_apply,
        fake_resolver_api(),
        "missing apply rejected before calls");

    auto missing_disable = fake_minhook_api();
    missing_disable.disable = nullptr;
    failures += expect_invalid_api_rejected(
        missing_disable,
        fake_resolver_api(),
        "missing rollback disable rejected before create");

    auto missing_remove = fake_minhook_api();
    missing_remove.remove = nullptr;
    failures += expect_invalid_api_rejected(
        missing_remove,
        fake_resolver_api(),
        "missing rollback remove rejected before create");

    FakeState disabled{};
    AudioHookFailure disabled_failure{
        AudioHookStage::ApplyQueued,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_ACCESS_DENIED,
        target,
    };
    failures += expect(
        install(false, disabled, &disabled_failure),
        "disabled install succeeds");
    failures += expect(
        disabled.module_calls == 0 && disabled.export_calls == 0,
        "disabled mode performs zero resolution");
    failures += expect(
        disabled.initialize_calls == 0 && disabled.created.empty() &&
            disabled.queued.empty() && disabled.apply_calls == 0 &&
            disabled.disabled.empty() && disabled.removed.empty(),
        "disabled mode performs zero MinHook calls");
    failures += expect(
        disabled.engine_factory_calls == 0,
        "disabled mode performs zero engine calls");
    failures += expect_failure(
        disabled_failure,
        AudioHookStage::None,
        MH_OK,
        ERROR_SUCCESS,
        nullptr,
        "disabled mode clears failure");
    failures += expect_rollback(
        disabled_failure,
        false,
        MH_OK,
        MH_OK,
        true,
        "disabled mode needs no rollback");

    FakeState success{};
    success.initialize_status = MH_ERROR_ALREADY_INITIALIZED;
    AudioHookFailure success_failure{};
    failures += expect(
        install(true, success, &success_failure),
        "already initialized MinHook is accepted");
    failures += expect(
        success.module_calls == 1 && success.module_name == L"dsound.dll",
        "resolve exact loaded dsound module");
    failures += expect(
        success.export_calls == 1 &&
            success.export_name == "DirectSoundCreate8",
        "resolve exact DirectSoundCreate8 export");
    failures += expect(
        only_target(success.created, target) &&
            only_target(success.queued, target) && success.apply_calls == 1,
        "create queue and apply exact resolved target");
    failures += expect(
        success.detour != nullptr && success.original_storage != nullptr,
        "create receives detour and original storage");
    failures += expect(
        success.disabled.empty() && success.removed.empty(),
        "successful install retains committed target");
    failures += expect(
        never_all_hooks(success),
        "successful install never uses MH_ALL_HOOKS");
    failures += expect(
        success.engine_factory_calls == 0,
        "hook installation performs no engine calls");

    using DirectSoundCreate8Fn = HRESULT (WINAPI*)(
        LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
    const auto installed_detour =
        reinterpret_cast<DirectSoundCreate8Fn>(success.detour);
    failures += expect(
        installed_detour(nullptr, nullptr, nullptr) == DSERR_INVALIDPARAM,
        "detour rejects null output pointer");

    GUID unexpected_device{};
    auto* output = reinterpret_cast<LPDIRECTSOUND8>(0x1);
    failures += expect(
        installed_detour(&unexpected_device, &output, nullptr) ==
            DSERR_NODRIVER,
        "detour rejects nondefault device");
    failures += expect(
        output == nullptr,
        "detour nulls output before rejecting device");

    output = reinterpret_cast<LPDIRECTSOUND8>(0x1);
    failures += expect(
        installed_detour(
            nullptr,
            &output,
            reinterpret_cast<LPUNKNOWN>(0x1)) == DSERR_NOAGGREGATION,
        "detour rejects aggregation");
    failures += expect(
        output == nullptr,
        "detour nulls output before rejecting aggregation");
    failures += expect_failure(
        success_failure,
        AudioHookStage::None,
        MH_OK,
        ERROR_SUCCESS,
        nullptr,
        "successful install clears failure");
    failures += expect_rollback(
        success_failure,
        false,
        MH_OK,
        MH_OK,
        true,
        "successful install needs no rollback");

    FakeState missing_module{};
    missing_module.module = nullptr;
    AudioHookFailure missing_module_failure{};
    failures += expect(
        !install(true, missing_module, &missing_module_failure),
        "missing module fails");
    failures += expect_failure(
        missing_module_failure,
        AudioHookStage::ResolveModule,
        MH_OK,
        ERROR_MOD_NOT_FOUND,
        nullptr,
        "missing module failure details");
    failures += expect(
        missing_module.export_calls == 0 &&
            missing_module.initialize_calls == 0,
        "missing module stops before export and MinHook");

    FakeState missing_export{};
    missing_export.export_address = nullptr;
    AudioHookFailure missing_export_failure{};
    failures += expect(
        !install(true, missing_export, &missing_export_failure),
        "missing export fails");
    failures += expect_failure(
        missing_export_failure,
        AudioHookStage::ResolveExport,
        MH_OK,
        ERROR_PROC_NOT_FOUND,
        nullptr,
        "missing export failure details");
    failures += expect(
        missing_export.initialize_calls == 0,
        "missing export stops before MinHook");

    FakeState initialize_failure{};
    initialize_failure.initialize_status = MH_ERROR_MEMORY_ALLOC;
    AudioHookFailure initialize_error{};
    failures += expect(
        !install(true, initialize_failure, &initialize_error),
        "initialize failure propagates");
    failures += expect_failure(
        initialize_error,
        AudioHookStage::InitializeMinHook,
        MH_ERROR_MEMORY_ALLOC,
        ERROR_SUCCESS,
        target,
        "initialize failure details");
    failures += expect(
        initialize_failure.created.empty() &&
            initialize_failure.disabled.empty() &&
            initialize_failure.removed.empty(),
        "initialize failure creates and removes nothing");

    FakeState create_failure{};
    create_failure.create_status = MH_ERROR_NOT_EXECUTABLE;
    AudioHookFailure create_error{};
    failures += expect(
        !install(true, create_failure, &create_error),
        "create failure propagates");
    failures += expect_failure(
        create_error,
        AudioHookStage::CreateHook,
        MH_ERROR_NOT_EXECUTABLE,
        ERROR_SUCCESS,
        target,
        "create failure details");
    failures += expect(
        only_target(create_failure.created, target) &&
            create_failure.disabled.empty() && create_failure.removed.empty(),
        "create failure removes nothing not created");

    FakeState queue_failure{};
    queue_failure.queue_status = MH_ERROR_MEMORY_PROTECT;
    AudioHookFailure queue_error{};
    failures += expect(
        !install(true, queue_failure, &queue_error),
        "queue failure propagates");
    failures += expect_failure(
        queue_error,
        AudioHookStage::QueueEnable,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_SUCCESS,
        target,
        "queue failure details");
    failures += expect(
        only_target(queue_failure.disabled, target) &&
            only_target(queue_failure.removed, target),
        "queue failure rolls back exact created target");
    failures += expect_rollback(
        queue_error,
        true,
        MH_OK,
        MH_OK,
        true,
        "queue rollback records clean statuses");
    failures += expect(
        never_all_hooks(queue_failure),
        "queue rollback never uses MH_ALL_HOOKS");

    FakeState apply_failure{};
    apply_failure.apply_status = MH_ERROR_MEMORY_PROTECT;
    AudioHookFailure apply_error{};
    failures += expect(
        !install(true, apply_failure, &apply_error),
        "apply failure propagates");
    failures += expect_failure(
        apply_error,
        AudioHookStage::ApplyQueued,
        MH_ERROR_MEMORY_PROTECT,
        ERROR_SUCCESS,
        target,
        "apply failure details");
    failures += expect(
        only_target(apply_failure.disabled, target) &&
            only_target(apply_failure.removed, target),
        "apply failure rolls back exact created target");
    failures += expect_rollback(
        apply_error,
        true,
        MH_OK,
        MH_OK,
        true,
        "apply rollback records clean statuses");
    failures += expect(
        never_all_hooks(apply_failure),
        "apply rollback never uses MH_ALL_HOOKS");
    failures += expect(
        apply_failure.engine_factory_calls == 0,
        "failure paths perform no engine calls");

    failures += exercise_rollback_statuses(
        true,
        MH_ERROR_MEMORY_PROTECT,
        MH_OK,
        true,
        "queue: disable failure and remove success is clean");
    failures += exercise_rollback_statuses(
        true,
        MH_OK,
        MH_ERROR_MEMORY_PROTECT,
        false,
        "queue: remove failure is incomplete");
    failures += exercise_rollback_statuses(
        true,
        MH_ERROR_MEMORY_PROTECT,
        MH_ERROR_MEMORY_PROTECT,
        false,
        "queue: both cleanup calls failing is incomplete");
    failures += exercise_rollback_statuses(
        false,
        MH_ERROR_MEMORY_PROTECT,
        MH_ERROR_NOT_CREATED,
        true,
        "apply: remove not-created is clean despite disable failure");
    failures += exercise_rollback_statuses(
        false,
        MH_OK,
        MH_ERROR_MEMORY_PROTECT,
        false,
        "apply: remove failure is incomplete");
    failures += exercise_rollback_statuses(
        false,
        MH_ERROR_MEMORY_PROTECT,
        MH_ERROR_MEMORY_PROTECT,
        false,
        "apply: both cleanup calls failing is incomplete");

    failures += test_detour_validation_and_success();
    failures += test_failure_reporting_and_cache();
    failures += test_concurrent_first_callers_share_initialization();
    failures += test_production_diagnostics_use_injected_platform_actions();
    failures += test_pacing_specific_diagnostics();
    failures += test_null_production_api_and_startup_fatal_reporting();
    failures += test_config_gate_and_attach_failure_policy();

    return failures == 0 ? 0 : 1;
}
