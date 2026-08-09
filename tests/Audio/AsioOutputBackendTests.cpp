// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioOutputBackend.h"
#include "Audio/Asio/AsioOutputBackendInternal.h"
#include "Audio/Asio/AsioSampleConverter.h"

#include "Audio/Mixer/AudioRenderCore.h"
#include "Audio/Mixer/AudioSnapshot.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using gc::audio::AsioCallbackRuntime;
using gc::audio::AsioCallbackRuntimeActions;
using gc::audio::AsioCapabilityReport;
using gc::audio::AsioDriverRegistration;
using gc::audio::AsioFailure;
using gc::audio::AsioFailureStage;
using gc::audio::AsioOutputBackend;
using gc::audio::AsioRuntimeCountersSnapshot;
using gc::audio::AsioStereoConversionResult;
using gc::audio::AudioRenderBlock;
using gc::audio::AudioRenderSilenceReason;
using gc::audio::AsioStreamRequest;
using gc::audio::AudioCursorResolutionKind;
using gc::audio::AudioCursorTimeline;
using gc::audio::AudioLockRegions;
using gc::audio::AudioSnapshot;
using gc::audio::IAsioDriver;
using gc::audio::IAsioDriverFactory;
using gc::audio::IAsioBlockRenderer;
using gc::audio::IAsioOutputObserver;
using gc::audio::IAsioRegistrySource;
using gc::audio::NormalizedSourceFormat;
using gc::audio::VoiceUsage;
using gc::audio::detail::AsioOutputBackendActions;

constexpr std::uint32_t kFrames = 192;
constexpr std::uint32_t kLatencyFrames = 384;
const auto kGameWindow = reinterpret_cast<HWND>(0x1234);

int Expect(bool condition, std::string_view name)
{
    if (condition)
    {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

void StoreNative64(ASIOSamples& destination, std::uint64_t value) noexcept
{
#if NATIVE_INT64
    destination = static_cast<ASIOSamples>(value);
#else
    destination.hi = static_cast<unsigned long>(value >> 32U);
    destination.lo = static_cast<unsigned long>(value);
#endif
}

void StoreNative64(ASIOTimeStamp& destination, std::uint64_t value) noexcept
{
#if NATIVE_INT64
    destination = static_cast<ASIOTimeStamp>(value);
#else
    destination.hi = static_cast<unsigned long>(value >> 32U);
    destination.lo = static_cast<unsigned long>(value);
#endif
}

struct CallbackRequest
{
    long buffer_index{};
    std::uint64_t sample_position{};
    std::uint64_t system_time_ns{};
    unsigned long flags{kSystemTimeValid | kSamplePositionValid};
};

struct DriverState
{
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> calls;
    std::vector<DWORD> lifecycle_threads;
    ASIOCallbacks* callbacks{};
    std::array<std::array<std::vector<std::byte>, 2>, 2> buffers;
    std::deque<CallbackRequest> queued_callbacks;
    std::thread callback_thread;
    std::atomic_bool start_returned{};
    std::atomic_bool stable_inside_start_dispatched{};
    std::atomic_uint32_t callbacks_processed{};
    bool stop_worker{};
    bool emit_priming{true};
    bool block_start_after_stable{};
    bool allow_start_return{true};
    std::optional<CallbackRequest> automatic_callback;
    ASIOBool init_result{ASIOTrue};
    ASIOError can_sample_rate_result{ASE_OK};
    ASIOError create_buffers_result{ASE_OK};
    ASIOError latency_result{ASE_OK};
    ASIOError start_result{ASE_OK};
    ASIOError stop_result{ASE_OK};
    ASIOError dispose_result{ASE_OK};
    ASIOError output_ready_probe_result{ASE_OK};
    ASIOError callback_output_ready_result{ASE_OK};
    double sample_rate{48'000.0};
    int output_ready_calls{};
    int stop_calls{};
    int dispose_calls{};
    bool released{};
    bool factory_failure{};
    bool registry_failure{};

    void Record(std::string name, bool lifecycle = false)
    {
        std::lock_guard lock(mutex);
        calls.push_back(std::move(name));
        if (lifecycle)
        {
            lifecycle_threads.push_back(GetCurrentThreadId());
        }
    }

    void Queue(CallbackRequest request)
    {
        {
            std::lock_guard lock(mutex);
            queued_callbacks.push_back(request);
        }
        condition.notify_all();
    }

    void Dispatch(const CallbackRequest& request)
    {
        ASIOTime time{};
        time.timeInfo.flags = request.flags;
        StoreNative64(time.timeInfo.samplePosition, request.sample_position);
        StoreNative64(time.timeInfo.systemTime, request.system_time_ns);
        callbacks->bufferSwitchTimeInfo(
            &time,
            request.buffer_index,
            ASIOTrue);
        callbacks_processed.fetch_add(1, std::memory_order_release);
        condition.notify_all();
    }

    void StartWorker()
    {
        callback_thread = std::thread([this]
        {
            for (;;)
            {
                CallbackRequest request;
                {
                    std::unique_lock lock(mutex);
                    condition.wait(lock, [&]
                    {
                        return stop_worker || !queued_callbacks.empty();
                    });
                    if (stop_worker && queued_callbacks.empty())
                    {
                        return;
                    }
                    request = queued_callbacks.front();
                    queued_callbacks.pop_front();
                }
                Dispatch(request);
            }
        });
    }

    void StopWorker()
    {
        {
            std::lock_guard lock(mutex);
            stop_worker = true;
        }
        condition.notify_all();
        if (callback_thread.joinable())
        {
            callback_thread.join();
        }
    }
};

class FakeDriver final : public IAsioDriver
{
public:
    explicit FakeDriver(std::shared_ptr<DriverState> state)
        : state_(std::move(state))
    {
    }

    ~FakeDriver() override
    {
        state_->StopWorker();
        state_->released = true;
        state_->Record("release", true);
    }

    ASIOBool Init(HWND system_reference) noexcept override
    {
        state_->Record("init", true);
        return system_reference == kGameWindow
            ? state_->init_result
            : ASIOFalse;
    }

    void GetDriverName(char (&name)[32]) noexcept override
    {
        CopyText("Fake Xonar ASIO", name, sizeof(name));
    }

    long GetDriverVersion() noexcept override
    {
        return 7;
    }

    void GetErrorMessage(char (&message)[124]) noexcept override
    {
        CopyText("fake ASIO error", message, sizeof(message));
    }

    ASIOError Start() noexcept override
    {
        state_->Record("start", true);
        if (state_->start_result != ASE_OK)
        {
            return state_->start_result;
        }
        if (state_->emit_priming)
        {
            state_->Dispatch({0, 0, 1'000'000'000});
            state_->Dispatch({1, 192, 1'000'000'000});
        }
        if (state_->block_start_after_stable)
        {
            state_->Dispatch({0, 384, 1'004'000'000});
            state_->stable_inside_start_dispatched.store(
                true,
                std::memory_order_release);
            state_->condition.notify_all();
            std::unique_lock lock(state_->mutex);
            state_->condition.wait(lock, [&]
            {
                return state_->allow_start_return;
            });
        }
        state_->StartWorker();
        state_->start_returned.store(true, std::memory_order_release);
        if (state_->automatic_callback)
        {
            state_->Queue(*state_->automatic_callback);
        }
        return ASE_OK;
    }

    ASIOError Stop() noexcept override
    {
        state_->Record("stop", true);
        ++state_->stop_calls;
        state_->StopWorker();
        return state_->stop_result;
    }

    ASIOError GetChannels(long* inputs, long* outputs) noexcept override
    {
        *inputs = 0;
        *outputs = 8;
        return ASE_OK;
    }

    ASIOError GetLatencies(long* input, long* output) noexcept override
    {
        state_->Record("getLatencies", true);
        *input = 0;
        *output = kLatencyFrames;
        return state_->latency_result;
    }

    ASIOError GetBufferSize(
        long* minimum,
        long* maximum,
        long* preferred,
        long* granularity) noexcept override
    {
        *minimum = 192;
        *maximum = 2400;
        *preferred = 192;
        *granularity = 1;
        return ASE_OK;
    }

    ASIOError CanSampleRate(ASIOSampleRate) noexcept override
    {
        return state_->can_sample_rate_result;
    }

    ASIOError GetSampleRate(ASIOSampleRate* rate) noexcept override
    {
        *rate = state_->sample_rate;
        return ASE_OK;
    }

    ASIOError SetSampleRate(ASIOSampleRate rate) noexcept override
    {
        state_->Record("setSampleRate", true);
        state_->sample_rate = rate;
        return ASE_OK;
    }

    ASIOError GetSamplePosition(
        ASIOSamples* samples,
        ASIOTimeStamp* timestamp) noexcept override
    {
        StoreNative64(*samples, 0);
        StoreNative64(*timestamp, 1'000'000'000);
        return ASE_OK;
    }

    ASIOError GetChannelInfo(ASIOChannelInfo* info) noexcept override
    {
        info->type = ASIOSTInt24LSB;
        const auto name = "Output " + std::to_string(info->channel);
        CopyText(name, info->name, sizeof(info->name));
        return ASE_OK;
    }

    ASIOError CreateBuffers(
        ASIOBufferInfo* buffers,
        long channel_count,
        long buffer_frames,
        ASIOCallbacks* callbacks) noexcept override
    {
        state_->Record("createBuffers", true);
        if (state_->create_buffers_result != ASE_OK)
        {
            return state_->create_buffers_result;
        }
        if (channel_count != 2 || buffer_frames != kFrames)
        {
            return ASE_InvalidParameter;
        }
        state_->callbacks = callbacks;
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            for (std::size_t index = 0; index < 2; ++index)
            {
                auto& storage = state_->buffers[channel][index];
                storage.assign(kFrames * 3, std::byte{0x7F});
                buffers[channel].buffers[index] = storage.data();
            }
        }
        return ASE_OK;
    }

    ASIOError DisposeBuffers() noexcept override
    {
        state_->Record("disposeBuffers", true);
        ++state_->dispose_calls;
        return state_->dispose_result;
    }

    ASIOError ControlPanel() noexcept override
    {
        state_->Record("controlPanel", true);
        return ASE_NotPresent;
    }

    ASIOError Future(long, void*) noexcept override
    {
        return ASE_SUCCESS;
    }

    ASIOError OutputReady() noexcept override
    {
        std::lock_guard lock(state_->mutex);
        ++state_->output_ready_calls;
        return state_->output_ready_calls == 1
            ? state_->output_ready_probe_result
            : state_->callback_output_ready_result;
    }

private:
    static void CopyText(
        std::string_view source,
        char* destination,
        std::size_t capacity) noexcept
    {
        std::fill_n(destination, capacity, '\0');
        const auto count = (std::min)(source.size(), capacity - 1);
        std::memcpy(destination, source.data(), count);
    }

    std::shared_ptr<DriverState> state_;
};

class FakeRegistry final : public IAsioRegistrySource
{
public:
    explicit FakeRegistry(std::shared_ptr<DriverState> state)
        : state_(std::move(state))
    {
    }

    std::expected<std::vector<gc::audio::AsioRegistryValue>, AsioFailure>
    Read32BitRegistrations() noexcept override
    {
        state_->Record("registry", true);
        if (state_->registry_failure)
        {
            return std::unexpected(AsioFailure{
                .stage = AsioFailureStage::registry,
                .detail = "injected registry failure",
            });
        }
        return std::vector<gc::audio::AsioRegistryValue>{
            {L"XONAR SOUND CARD",
             L"{12345678-1234-4321-8765-1234567890AB}"},
        };
    }

private:
    std::shared_ptr<DriverState> state_;
};

class FakeFactory final : public IAsioDriverFactory
{
public:
    explicit FakeFactory(std::shared_ptr<DriverState> state)
        : state_(std::move(state))
    {
    }

    std::expected<std::unique_ptr<IAsioDriver>, AsioFailure>
    Create(const CLSID&) noexcept override
    {
        state_->Record("factory", true);
        if (state_->factory_failure)
        {
            return std::unexpected(AsioFailure{
                .stage = AsioFailureStage::com,
                .detail = "injected factory failure",
            });
        }
        std::unique_ptr<IAsioDriver> driver =
            std::make_unique<FakeDriver>(state_);
        return driver;
    }

private:
    std::shared_ptr<DriverState> state_;
};

struct ObserverState
{
    std::mutex mutex;
    std::atomic_int startup_successes{};
    std::atomic_int summaries{};
    std::atomic_int runtime_failures{};
    std::optional<AsioCapabilityReport> report;
    std::optional<AsioRuntimeCountersSnapshot> last_counters;
    std::optional<AsioFailure> last_failure;
};

class FakeObserver final : public IAsioOutputObserver
{
public:
    explicit FakeObserver(std::shared_ptr<ObserverState> state)
        : state_(std::move(state))
    {
    }

    void StartupSucceeded(const AsioCapabilityReport& report) noexcept override
    {
        std::lock_guard lock(state_->mutex);
        state_->report = report;
        state_->startup_successes.fetch_add(1, std::memory_order_release);
    }

    void RuntimeSummary(
        const AsioRuntimeCountersSnapshot& counters) noexcept override
    {
        std::lock_guard lock(state_->mutex);
        state_->last_counters = counters;
        state_->summaries.fetch_add(1, std::memory_order_release);
    }

    void RuntimeFailed(
        const AsioFailure& failure,
        const AsioRuntimeCountersSnapshot& counters) noexcept override
    {
        std::lock_guard lock(state_->mutex);
        state_->last_failure = failure;
        state_->last_counters = counters;
        state_->runtime_failures.fetch_add(1, std::memory_order_release);
    }

private:
    std::shared_ptr<ObserverState> state_;
};

struct ActionState
{
    HRESULT com_result{S_OK};
    std::atomic_uint32_t com_initializations{};
    std::atomic_uint32_t com_uninitializations{};
    std::atomic<DWORD> com_thread{};
    std::atomic<DWORD> com_flags{};
    std::atomic_uint32_t events_created{};
    std::atomic_uint32_t events_closed{};
    std::atomic_uint32_t message_waits{};
    std::atomic_uint32_t messages_drained{};
    std::atomic_bool inject_message_wake{true};
    std::atomic_uint32_t worker_promotions{};
    std::atomic_uint32_t worker_reversions{};
    std::atomic_bool fail_worker_promotion{};
    std::atomic_uint64_t qpc_tick{100};
    std::atomic_uint32_t now_ms{1004};
};

HRESULT InitializeCom(void* context, DWORD coinit_flags) noexcept
{
    auto& state = *static_cast<ActionState*>(context);
    state.com_initializations.fetch_add(1, std::memory_order_relaxed);
    state.com_thread.store(GetCurrentThreadId(), std::memory_order_release);
    state.com_flags.store(coinit_flags, std::memory_order_release);
    return state.com_result;
}

void UninitializeCom(void* context) noexcept
{
    static_cast<ActionState*>(context)->com_uninitializations.fetch_add(
        1, std::memory_order_relaxed);
}

HANDLE CreateManualEvent(void* context) noexcept
{
    auto& state = *static_cast<ActionState*>(context);
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event != nullptr)
    {
        state.events_created.fetch_add(1, std::memory_order_relaxed);
    }
    return event;
}

bool SignalEvent(void*, HANDLE event) noexcept
{
    return SetEvent(event) != FALSE;
}

DWORD WaitForEvent(void*, HANDLE event, DWORD timeout_ms) noexcept
{
    return WaitForSingleObject(event, timeout_ms);
}

void CloseTrackedHandle(void* context, HANDLE handle) noexcept
{
    if (handle != nullptr && CloseHandle(handle))
    {
        static_cast<ActionState*>(context)->events_closed.fetch_add(
            1, std::memory_order_relaxed);
    }
}

DWORD MessageWait(
    void* context,
    std::span<const HANDLE> handles,
    DWORD timeout_ms) noexcept
{
    static_cast<ActionState*>(context)->message_waits.fetch_add(
        1, std::memory_order_relaxed);
    if (static_cast<ActionState*>(context)->inject_message_wake.exchange(
            false,
            std::memory_order_acq_rel))
    {
        return WAIT_OBJECT_0 + static_cast<DWORD>(handles.size());
    }
    return MsgWaitForMultipleObjectsEx(
        static_cast<DWORD>(handles.size()),
        handles.data(),
        timeout_ms,
        QS_ALLINPUT,
        MWMO_INPUTAVAILABLE);
}

void DrainMessages(void* context) noexcept
{
    static_cast<ActionState*>(context)->messages_drained.fetch_add(
        1, std::memory_order_relaxed);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

std::uint64_t TickCountMs(void*) noexcept
{
    return GetTickCount64();
}

std::uint32_t TimeGetTimeMs(void* context) noexcept
{
    return static_cast<ActionState*>(context)->now_ms.load(
        std::memory_order_relaxed);
}

bool QueryCounter(void* context, std::uint64_t* value) noexcept
{
    if (value == nullptr)
    {
        return false;
    }
    *value = static_cast<ActionState*>(context)->qpc_tick.fetch_add(
        10, std::memory_order_relaxed);
    return true;
}

bool QueryFrequency(void*, std::uint64_t* value) noexcept
{
    if (value == nullptr)
    {
        return false;
    }
    *value = 10'000'000;
    return true;
}

void* PromoteWorker(
    void* context,
    const wchar_t*,
    std::uint32_t*) noexcept
{
    auto& state = *static_cast<ActionState*>(context);
    state.worker_promotions.fetch_add(1, std::memory_order_relaxed);
    return state.fail_worker_promotion.load(std::memory_order_relaxed)
        ? nullptr
        : reinterpret_cast<void*>(0x1234);
}

bool RevertWorker(void* context, void*) noexcept
{
    static_cast<ActionState*>(context)->worker_reversions.fetch_add(
        1, std::memory_order_relaxed);
    return true;
}

AsioOutputBackendActions Actions(ActionState& state) noexcept
{
    return {
        .context = &state,
        .initialize_com = &InitializeCom,
        .uninitialize_com = &UninitializeCom,
        .create_manual_event = &CreateManualEvent,
        .signal_event = &SignalEvent,
        .wait_for_event = &WaitForEvent,
        .close_handle = &CloseTrackedHandle,
        .message_wait = &MessageWait,
        .drain_messages = &DrainMessages,
        .tick_count_ms = &TickCountMs,
        .time_get_time_ms = &TimeGetTimeMs,
        .callback_runtime_actions = AsioCallbackRuntimeActions{
            &state,
            &QueryCounter,
            &QueryFrequency,
            &PromoteWorker,
            &RevertWorker,
        },
        .summary_interval_ms = 30'000,
    };
}

AsioStreamRequest Request()
{
    return {
        .driver_name = "XONAR SOUND CARD",
        .buffer_frames = kFrames,
        .output_base_channel = 0,
    };
}

std::unique_ptr<AsioOutputBackend> StartBackend(
    const std::shared_ptr<DriverState>& driver,
    const std::shared_ptr<ObserverState>& observer,
    ActionState& actions,
    DWORD timeout_ms,
    AsioFailure& failure,
    std::shared_ptr<const ma_allocation_callbacks> allocations = {})
{
    return gc::audio::detail::StartAsioOutputBackendAndWait(
        kGameWindow,
        Request(),
        std::make_unique<FakeRegistry>(driver),
        std::make_unique<FakeFactory>(driver),
        std::make_shared<FakeObserver>(observer),
        std::move(allocations),
        timeout_ms,
        Actions(actions),
        &failure);
}

WAVEFORMATEX StereoPcm16(std::uint32_t sample_rate)
{
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;
    return format;
}

int TestRenderDiagnosticsAccumulator()
{
    gc::audio::detail::AsioRenderDiagnostics diagnostics;
    diagnostics.RecordRender(AudioRenderBlock{
        .mixer_result = MA_SUCCESS,
        .active_voices = 0,
        .missing_frames = 7,
        .silence_reason = AudioRenderSilenceReason::no_active_voice,
        .silence_substituted = true,
    });
    diagnostics.RecordRender(AudioRenderBlock{
        .mixer_result = MA_SUCCESS,
        .active_voices = 1,
        .missing_frames = 11,
        .silence_reason = AudioRenderSilenceReason::active_short_read,
        .silence_substituted = true,
    });
    diagnostics.RecordRender(AudioRenderBlock{
        .mixer_result = MA_INVALID_ARGS,
        .active_voices = 1,
        .silence_reason = AudioRenderSilenceReason::mixer_error,
        .silence_substituted = true,
    });
    diagnostics.RecordRender(AudioRenderBlock{
        .mixer_result = MA_OUT_OF_MEMORY,
        .active_voices = 1,
        .silence_reason = AudioRenderSilenceReason::mixer_error,
        .silence_substituted = true,
    });
    diagnostics.RecordRender(AudioRenderBlock{
        .mixer_result = MA_SUCCESS,
        .silence_reason = AudioRenderSilenceReason::render_contract_error,
        .silence_substituted = true,
    });
    diagnostics.RecordRender(AudioRenderBlock{
        .mixer_result = MA_SUCCESS,
        .silence_reason = AudioRenderSilenceReason::none,
    });

    AudioRenderBlock active_block{
        .mixer_result = MA_SUCCESS,
        .active_voices = 2,
        .silence_reason = AudioRenderSilenceReason::none,
    };
    diagnostics.RecordConversion(
        active_block,
        AsioStereoConversionResult{
            .converted = true,
            .stats = {
                .clipped_samples =
                    (std::numeric_limits<std::uint64_t>::max)() - 2,
                .maximum_absolute_sample = 1.25F,
            },
        });
    diagnostics.RecordConversion(
        active_block,
        AsioStereoConversionResult{
            .converted = true,
            .stats = {
                .clipped_samples = 10,
                .maximum_absolute_sample = 1.0F,
            },
        });
    diagnostics.RecordConversion(
        active_block,
        AsioStereoConversionResult{
            .converted = true,
            .stats = {.all_zero = true},
        });
    AudioRenderBlock inactive_block{
        .mixer_result = MA_SUCCESS,
        .active_voices = 0,
        .silence_reason = AudioRenderSilenceReason::none,
    };
    diagnostics.RecordConversion(
        inactive_block,
        AsioStereoConversionResult{
            .converted = true,
            .stats = {.all_zero = true},
        });
    AudioRenderBlock substituted_block{
        .mixer_result = MA_SUCCESS,
        .active_voices = 1,
        .silence_reason = AudioRenderSilenceReason::active_short_read,
        .silence_substituted = true,
    };
    diagnostics.RecordConversion(
        substituted_block,
        AsioStereoConversionResult{
            .converted = true,
            .stats = {
                .clipped_samples = 5,
                .maximum_absolute_sample = 2.0F,
                .all_zero = true,
            },
        });
    diagnostics.RecordConversion(
        active_block,
        AsioStereoConversionResult{
            .converted = false,
            .stats = {.non_finite = true},
        });

    const auto snapshot = diagnostics.Snapshot();
    int failures = Expect(
        snapshot.no_active_voice_silence_blocks == 1 &&
            snapshot.active_short_read_blocks == 1 &&
            snapshot.mixer_error_blocks == 2 &&
            snapshot.render_contract_error_blocks == 1 &&
            snapshot.short_read_missing_frames == 18 &&
            snapshot.first_mixer_error == MA_INVALID_ARGS,
        "render silence reasons are exclusive and first mixer error is retained");
    failures += Expect(
        snapshot.clipped_output_blocks == 2 &&
            snapshot.clipped_output_samples ==
                (std::numeric_limits<std::uint64_t>::max)() &&
            snapshot.maximum_absolute_output_sample == 1.25F &&
            snapshot.zero_output_blocks_with_active_voice == 1 &&
            snapshot.zero_output_blocks_without_active_voice == 1 &&
            snapshot.non_finite_output_blocks == 1,
        "conversion integrity counters saturate and exclude substituted silence");
    return failures;
}

int TestFakeXonarEndToEnd()
{
    auto driver = std::make_shared<DriverState>();
    auto observer = std::make_shared<ObserverState>();
    ActionState actions;
    AsioFailure failure;

    auto future = std::async(std::launch::async, [&]
    {
        return StartBackend(driver, observer, actions, 2'000, failure);
    });
    int failures{};
    failures += Expect(
        WaitUntil([&]
        {
            return driver->start_returned.load(std::memory_order_acquire) &&
                driver->callbacks_processed.load(std::memory_order_acquire) ==
                    2;
        }),
        "two priming callbacks complete after ASIO start returns");
    failures += Expect(
        future.wait_for(20ms) == std::future_status::timeout &&
            observer->startup_successes == 0 &&
            std::ranges::all_of(driver->buffers[0][0], [](std::byte value)
            {
                return value == std::byte{0};
            }) &&
            std::ranges::all_of(driver->buffers[1][1], [](std::byte value)
            {
                return value == std::byte{0};
            }),
        "startup remains uncommitted and priming submits only silence");

    driver->Queue({0, 384, 1'004'000'000});
    auto backend = future.get();
    failures += Expect(
        backend != nullptr && failure.stage == AsioFailureStage::none &&
            observer->startup_successes == 1 && observer->report.has_value() &&
            observer->report->output_channels.size() == 8 &&
            observer->report->effective_buffer_frames == kFrames &&
            observer->report->output_latency_frames == kLatencyFrames,
        "third stable callback commits the generic fake-Xonar backend");
    if (backend == nullptr)
    {
        return failures + 1;
    }
    failures += Expect(
        backend->endpoint_buffer_frames() == kFrames &&
            backend->output_sample_rate() == 48'000 &&
            backend->CurrentOutputFrame() == 384,
        "service contract exposes exact period rate and presentation anchor");

    NormalizedSourceFormat normalized{};
    const auto wave = StereoPcm16(48'000);
    failures += Expect(
        gc::audio::NormalizeSourceFormat(&wave, &normalized) == DS_OK,
        "backend test source normalizes");
    std::vector<std::int16_t> samples(kFrames * 2 * 2);
    for (std::size_t frame = 0; frame < samples.size() / 2; ++frame)
    {
        samples[frame * 2] = 16'384;
        samples[frame * 2 + 1] = -16'384;
    }
    auto snapshot = std::make_shared<AudioSnapshot>(
        static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t)),
        normalized.block_align);
    AudioLockRegions regions{};
    failures += Expect(
        snapshot->Lock(
            0,
            static_cast<DWORD>(samples.size() * sizeof(std::int16_t)),
            DSBLOCK_ENTIREBUFFER,
            &regions) == DS_OK,
        "backend test source locks");
    std::memcpy(regions.first, samples.data(), regions.first_bytes);
    if (regions.second_bytes != 0)
    {
        std::memcpy(
            regions.second,
            reinterpret_cast<const std::byte*>(samples.data()) +
                regions.first_bytes,
            regions.second_bytes);
    }
    failures += Expect(
        snapshot->Unlock(
            regions.first,
            regions.first_bytes,
            regions.second,
            regions.second_bytes) == DS_OK,
        "backend test source unlocks");
    auto timeline = std::make_shared<AudioCursorTimeline>();
    ma_result mixer_result = MA_ERROR;
    auto voice = backend->CreateVoice(
        normalized,
        snapshot,
        timeline,
        VoiceUsage::General,
        &mixer_result);
    failures += Expect(
        mixer_result == MA_SUCCESS && voice != nullptr &&
            voice->Play(false, 99) == DS_OK,
        "ASIO service creates and starts a mixer voice");

    actions.now_ms.store(1008, std::memory_order_relaxed);
    driver->Queue({1, 576, 1'008'000'000});
    failures += Expect(
        WaitUntil([&]
        {
            return driver->callbacks_processed.load(
                       std::memory_order_acquire) == 4;
        }),
        "post-commit ASIO callback renders");
    const auto& left = driver->buffers[0][1];
    const auto& right = driver->buffers[1][1];
    bool packed_samples_match = left.size() == kFrames * 3 &&
        right.size() == kFrames * 3;
    for (std::size_t frame = 0; packed_samples_match && frame < kFrames;
         ++frame)
    {
        packed_samples_match =
            left[frame * 3] == std::byte{0x00} &&
            left[frame * 3 + 1] == std::byte{0x00} &&
            left[frame * 3 + 2] == std::byte{0x40} &&
            right[frame * 3] == std::byte{0x00} &&
            right[frame * 3 + 1] == std::byte{0x00} &&
            right[frame * 3 + 2] == std::byte{0xC0};
    }
    const auto resolved = timeline->ResolveSourceFrame(960, 99, kFrames * 2);
    failures += Expect(
        packed_samples_match &&
            resolved.kind == AudioCursorResolutionKind::Resolved &&
            resolved.source_frame == 0,
        "stable render uses sample position plus latency and converts packed 24-bit stereo");

    voice.reset();
    auto create_constant_voice = [&](std::int16_t left_sample,
                                     std::int16_t right_sample,
                                     std::uint32_t play_time_ms)
    {
        std::vector<std::int16_t> constant_samples(kFrames * 2 * 2);
        for (std::size_t frame{};
             frame < constant_samples.size() / 2;
             ++frame)
        {
            constant_samples[frame * 2] = left_sample;
            constant_samples[frame * 2 + 1] = right_sample;
        }
        auto constant_snapshot = std::make_shared<AudioSnapshot>(
            static_cast<std::uint32_t>(
                constant_samples.size() * sizeof(std::int16_t)),
            normalized.block_align);
        AudioLockRegions constant_regions{};
        if (constant_snapshot->Lock(
                0,
                static_cast<DWORD>(
                    constant_samples.size() * sizeof(std::int16_t)),
                DSBLOCK_ENTIREBUFFER,
                &constant_regions) != DS_OK)
        {
            ++failures;
            return std::unique_ptr<gc::audio::MixerVoice>{};
        }
        std::memcpy(
            constant_regions.first,
            constant_samples.data(),
            constant_regions.first_bytes);
        if (constant_regions.second_bytes != 0)
        {
            std::memcpy(
                constant_regions.second,
                reinterpret_cast<const std::byte*>(constant_samples.data()) +
                    constant_regions.first_bytes,
                constant_regions.second_bytes);
        }
        if (constant_snapshot->Unlock(
                constant_regions.first,
                constant_regions.first_bytes,
                constant_regions.second,
                constant_regions.second_bytes) != DS_OK)
        {
            ++failures;
            return std::unique_ptr<gc::audio::MixerVoice>{};
        }
        ma_result create_result = MA_ERROR;
        auto constant_voice = backend->CreateVoice(
            normalized,
            constant_snapshot,
            std::make_shared<AudioCursorTimeline>(),
            VoiceUsage::General,
            &create_result);
        if (create_result != MA_SUCCESS || constant_voice == nullptr ||
            constant_voice->Play(false, play_time_ms) != DS_OK)
        {
            ++failures;
            return std::unique_ptr<gc::audio::MixerVoice>{};
        }
        return constant_voice;
    };

    auto clipped_voice_a = create_constant_voice(32'767, 32'767, 1008);
    auto clipped_voice_b = create_constant_voice(32'767, 32'767, 1008);
    actions.now_ms.store(1012, std::memory_order_relaxed);
    driver->Queue({0, 768, 1'012'000'000});
    failures += Expect(
        clipped_voice_a != nullptr && clipped_voice_b != nullptr &&
            WaitUntil([&]
            {
                return driver->callbacks_processed.load(
                           std::memory_order_acquire) == 5;
            }),
        "two full-scale voices render one clipped ASIO block");
    clipped_voice_a.reset();
    clipped_voice_b.reset();

    auto zero_voice = create_constant_voice(0, 0, 1012);
    actions.now_ms.store(1016, std::memory_order_relaxed);
    driver->Queue({1, 960, 1'016'000'000});
    failures += Expect(
        zero_voice != nullptr && WaitUntil([&]
        {
            return driver->callbacks_processed.load(
                       std::memory_order_acquire) == 6;
        }),
        "active exact-zero voice renders one zero ASIO block");
    zero_voice.reset();

    backend->CountPendingCursorQuery();
    backend->CountUnmappedCursorFailure();
    voice.reset();
    backend.reset();
    if (observer->last_counters &&
        !(observer->runtime_failures == 0 && observer->summaries == 1 &&
          observer->last_counters->callbacks == 6 &&
          observer->last_counters->time_info_callbacks == 6 &&
          observer->last_counters->legacy_callbacks == 0 &&
          observer->last_counters->silence_substitutions == 1 &&
          observer->last_counters->pending_cursor_queries == 1 &&
          observer->last_counters->unmapped_cursor_failures == 1 &&
          observer->last_counters->maximum_callback_ticks > 0 &&
          observer->last_counters->callback_interval_samples == 5 &&
          observer->last_counters->timed_callback_work_samples == 6 &&
          observer->last_counters->timed_render_work_samples == 6 &&
          observer->last_counters->driver_interval_samples == 3 &&
          observer->last_counters->expected_period_ns == 4'000'000 &&
          observer->last_counters->buffer_alternation_violations == 0 &&
          observer->last_counters->no_active_voice_silence_blocks == 1 &&
          observer->last_counters->active_short_read_blocks == 0 &&
          observer->last_counters->mixer_error_blocks == 0 &&
          observer->last_counters->render_contract_error_blocks == 0 &&
          observer->last_counters->clipped_output_blocks == 1 &&
          observer->last_counters->clipped_output_samples > 0 &&
          observer->last_counters->maximum_absolute_output_sample > 1.0F &&
          observer->last_counters->zero_output_blocks_with_active_voice == 1 &&
          observer->last_counters->zero_output_blocks_without_active_voice == 0))
    {
        const auto& counters = *observer->last_counters;
        std::cerr << "Final counters: summaries=" << observer->summaries
                  << " failures=" << observer->runtime_failures
                  << " callbacks=" << counters.callbacks
                  << " time_info=" << counters.time_info_callbacks
                  << " legacy=" << counters.legacy_callbacks
                  << " silence=" << counters.silence_substitutions
                  << " pending=" << counters.pending_cursor_queries
                  << " unmapped=" << counters.unmapped_cursor_failures
                  << " callback_ticks=" << counters.maximum_callback_ticks
                  << " native_rate="
                  << counters.mixer.native_rate_buffers << '\n';
    }
    failures += Expect(
        observer->runtime_failures == 0 && observer->summaries == 1 &&
            observer->last_counters.has_value() &&
            observer->last_counters->callbacks == 6 &&
            observer->last_counters->time_info_callbacks == 6 &&
            observer->last_counters->legacy_callbacks == 0 &&
            observer->last_counters->silence_substitutions == 1 &&
            observer->last_counters->pending_cursor_queries == 1 &&
            observer->last_counters->unmapped_cursor_failures == 1 &&
            observer->last_counters->maximum_callback_ticks > 0 &&
            observer->last_counters->callback_interval_samples == 5 &&
            observer->last_counters->timed_callback_work_samples == 6 &&
            observer->last_counters->timed_render_work_samples == 6 &&
            observer->last_counters->driver_interval_samples == 3 &&
            observer->last_counters->expected_period_ns == 4'000'000 &&
            observer->last_counters->buffer_alternation_violations == 0 &&
            observer->last_counters->no_active_voice_silence_blocks == 1 &&
            observer->last_counters->active_short_read_blocks == 0 &&
            observer->last_counters->mixer_error_blocks == 0 &&
            observer->last_counters->render_contract_error_blocks == 0 &&
            observer->last_counters->clipped_output_blocks == 1 &&
            observer->last_counters->clipped_output_samples > 0 &&
            observer->last_counters->maximum_absolute_output_sample > 1.0F &&
            observer->last_counters->zero_output_blocks_with_active_voice == 1 &&
            observer->last_counters->zero_output_blocks_without_active_voice == 0,
        "final control-thread summary carries complete callback and mixer counters");
    failures += Expect(
        driver->stop_calls == 1 && driver->dispose_calls == 1 &&
            driver->released &&
            actions.worker_promotions.load() == 1 &&
            actions.worker_reversions.load() == 1 &&
            actions.com_initializations.load() == 1 &&
            actions.com_uninitializations.load() == 1 &&
            actions.com_flags.load() ==
                (COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) &&
            actions.events_created.load() == actions.events_closed.load() &&
            actions.messages_drained.load() >= 1 &&
            !driver->lifecycle_threads.empty() &&
            std::ranges::all_of(
                driver->lifecycle_threads,
                [&](DWORD thread)
                {
                    return thread == actions.com_thread.load();
                }),
        "teardown joins workers and keeps every lifecycle call on the COM thread");
    return failures;
}

int TestStableCallbackCannotCommitInsideDriverStart()
{
    auto driver = std::make_shared<DriverState>();
    driver->block_start_after_stable = true;
    driver->allow_start_return = false;
    auto observer = std::make_shared<ObserverState>();
    ActionState actions;
    AsioFailure failure;
    auto future = std::async(std::launch::async, [&]
    {
        return StartBackend(driver, observer, actions, 2'000, failure);
    });

    int failures = Expect(
        WaitUntil([&]
        {
            return driver->stable_inside_start_dispatched.load(
                std::memory_order_acquire);
        }) && future.wait_for(20ms) == std::future_status::timeout &&
            observer->startup_successes == 0,
        "stable callback cannot commit while ASIO start remains on the stack");
    {
        std::lock_guard lock(driver->mutex);
        driver->allow_start_return = true;
    }
    driver->condition.notify_all();
    auto backend = future.get();
    failures += Expect(
        backend != nullptr && observer->startup_successes == 1,
        "stable callback commits only after ASIO start returns successfully");
    backend.reset();
    return failures;
}

void* FailAllocate(std::size_t, void*)
{
    return nullptr;
}

void* FailReallocate(void*, std::size_t, void*)
{
    return nullptr;
}

void FailFree(void*, void*)
{
}

enum class FailureCase
{
    com,
    registry,
    factory,
    init,
    capability,
    mmcss,
    create_buffers,
    latency,
    render_core,
    start,
    no_callback,
    priming_only,
    invalid_clock,
    callback_fault,
};

int ExpectPrecommitFailure(
    FailureCase failure_case,
    AsioFailureStage expected_stage,
    std::string_view name)
{
    auto driver = std::make_shared<DriverState>();
    auto observer = std::make_shared<ObserverState>();
    ActionState actions;
    std::shared_ptr<const ma_allocation_callbacks> allocations;
    switch (failure_case)
    {
    case FailureCase::com:
        actions.com_result = E_FAIL;
        break;
    case FailureCase::registry:
        driver->registry_failure = true;
        break;
    case FailureCase::factory:
        driver->factory_failure = true;
        break;
    case FailureCase::init:
        driver->init_result = ASIOFalse;
        break;
    case FailureCase::capability:
        driver->can_sample_rate_result = ASE_NoClock;
        break;
    case FailureCase::mmcss:
        actions.fail_worker_promotion.store(true);
        break;
    case FailureCase::create_buffers:
        driver->create_buffers_result = ASE_HWMalfunction;
        break;
    case FailureCase::latency:
        driver->latency_result = ASE_HWMalfunction;
        break;
    case FailureCase::render_core:
    {
        const ma_allocation_callbacks failing{
            nullptr,
            &FailAllocate,
            &FailReallocate,
            &FailFree,
        };
        allocations =
            std::make_shared<const ma_allocation_callbacks>(failing);
        break;
    }
    case FailureCase::start:
        driver->start_result = ASE_HWMalfunction;
        break;
    case FailureCase::no_callback:
        driver->emit_priming = false;
        driver->sample_rate = 44'100.0;
        break;
    case FailureCase::priming_only:
        break;
    case FailureCase::invalid_clock:
        driver->automatic_callback = CallbackRequest{
            0,
            384,
            1'004'000'000,
            kSamplePositionValid,
        };
        break;
    case FailureCase::callback_fault:
        driver->automatic_callback = CallbackRequest{
            2,
            384,
            1'004'000'000,
        };
        break;
    }

    AsioFailure failure;
    auto backend = StartBackend(
        driver,
        observer,
        actions,
        30,
        failure,
        std::move(allocations));
    int failures = Expect(
        backend == nullptr && failure.stage == expected_stage &&
            observer->startup_successes == 0 &&
            observer->runtime_failures == 0,
        name);
    failures += Expect(
        actions.events_created.load() == actions.events_closed.load() &&
            actions.com_initializations.load() == 1 &&
            actions.com_uninitializations.load() ==
                (failure_case == FailureCase::com ? 0U : 1U),
        "precommit failure balances event and COM ownership");
    const bool driver_created = failure_case != FailureCase::com &&
        failure_case != FailureCase::registry &&
        failure_case != FailureCase::factory;
    failures += Expect(
        !driver_created || driver->released,
        "precommit failure releases an instantiated driver");
    const bool start_succeeded = failure_case == FailureCase::no_callback ||
        failure_case == FailureCase::priming_only ||
        failure_case == FailureCase::invalid_clock ||
        failure_case == FailureCase::callback_fault;
    failures += Expect(
        !start_succeeded ||
            (driver->stop_calls == 1 && driver->dispose_calls == 1),
        "failure after start stops and disposes before returning");
    failures += Expect(
        failure_case != FailureCase::no_callback ||
            driver->sample_rate == 44'100.0,
        "failure after changing sample rate restores the original rate");
    return failures;
}

int TestPrecommitFailureMatrix()
{
    int failures{};
    failures += ExpectPrecommitFailure(
        FailureCase::com, AsioFailureStage::com, "COM failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::registry,
        AsioFailureStage::registry,
        "registration resolution failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::factory,
        AsioFailureStage::com,
        "driver creation failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::init, AsioFailureStage::init, "init failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::capability,
        AsioFailureStage::sample_rate,
        "capability failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::mmcss,
        AsioFailureStage::callback_prepare,
        "deferred worker MMCSS failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::create_buffers,
        AsioFailureStage::create_buffers,
        "buffer creation failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::latency,
        AsioFailureStage::latency,
        "latency query failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::render_core,
        AsioFailureStage::render_core,
        "render core allocation failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::start, AsioFailureStage::start, "start failure is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::no_callback,
        AsioFailureStage::startup_clock,
        "no callback before the deadline is recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::priming_only,
        AsioFailureStage::startup_clock,
        "only priming callbacks before the deadline are recoverable");
    failures += ExpectPrecommitFailure(
        FailureCase::invalid_clock,
        AsioFailureStage::runtime_clock,
        "invalid third clock is recoverable before commit");
    failures += ExpectPrecommitFailure(
        FailureCase::callback_fault,
        AsioFailureStage::callback,
        "callback fault is recoverable before commit");
    return failures;
}

class InertRenderer final : public IAsioBlockRenderer
{
public:
    void RenderAsioBlock(
        const gc::audio::AsioRenderRequest&) noexcept override
    {
    }

    void ClearAsioBlock(long) noexcept override
    {
    }

    void OnAsioRuntimeFault(AsioFailureStage) noexcept override
    {
    }
};

ASIOError InertLegacyPosition(
    void*,
    ASIOSamples* samples,
    ASIOTimeStamp* timestamp) noexcept
{
    StoreNative64(*samples, 0);
    StoreNative64(*timestamp, 1);
    return ASE_OK;
}

int TestCallbackRouterCollisionIsRecoverable()
{
    ActionState actions;
    InertRenderer renderer;
    auto prepared_incumbent = AsioCallbackRuntime::Prepare(
        renderer,
        {nullptr, &InertLegacyPosition},
        {192, 48'000},
        Actions(actions).callback_runtime_actions);
    if (!prepared_incumbent)
    {
        return Expect(false, "incumbent callback router installs");
    }
    auto incumbent = std::move(*prepared_incumbent);
    if (!incumbent->Install())
    {
        return Expect(false, "incumbent callback router installs");
    }

    auto driver = std::make_shared<DriverState>();
    auto observer = std::make_shared<ObserverState>();
    AsioFailure failure;
    auto backend = StartBackend(driver, observer, actions, 30, failure);
    int failures = Expect(
        backend == nullptr &&
            failure.stage == AsioFailureStage::callback_prepare &&
            observer->startup_successes == 0 && driver->released,
        "a process-global callback router collision fails before start");

    incumbent->BeginStopping();
    incumbent->JoinWorker();
    incumbent->Uninstall();
    incumbent.reset();
    failures += Expect(
        actions.worker_promotions.load() == 2 &&
            actions.worker_reversions.load() == 2 &&
            actions.events_created.load() == actions.events_closed.load(),
        "router collision tears down only its own worker and leaves no handles");
    return failures;
}

int TestPostCommitFaultStopsWithoutRecommit()
{
    auto driver = std::make_shared<DriverState>();
    driver->automatic_callback = CallbackRequest{
        0,
        384,
        1'004'000'000,
    };
    auto observer = std::make_shared<ObserverState>();
    ActionState actions;
    AsioFailure failure;
    auto backend = StartBackend(driver, observer, actions, 2'000, failure);
    if (backend == nullptr)
    {
        return Expect(false, "post-commit fault backend starts");
    }

    const long acknowledged = AsioCallbackRuntime::Callbacks()->asioMessage(
        kAsioResetRequest,
        0,
        nullptr,
        nullptr);
    int failures = Expect(
        acknowledged == 1 && WaitUntil([&]
        {
            return observer->runtime_failures == 1;
        }),
        "post-commit reset request wakes the control thread");
    failures += Expect(
        observer->startup_successes == 1 && observer->summaries == 0 &&
            observer->last_failure.has_value() &&
            observer->last_failure->stage == AsioFailureStage::callback &&
            observer->last_counters.has_value() &&
            observer->last_counters->reset_requests == 1 &&
            driver->stop_calls == 1 && driver->dispose_calls == 1 &&
            driver->released,
        "post-commit fault reports restart-required diagnostics after teardown");
    backend.reset();
    failures += Expect(
        observer->startup_successes == 1 &&
            observer->runtime_failures == 1,
        "committed backend never commits or reports the same failure twice");
    return failures;
}

} // namespace

int main()
{
    int failures{};
    failures += TestRenderDiagnosticsAccumulator();
    failures += TestFakeXonarEndToEnd();
    failures += TestStableCallbackCannotCommitInsideDriverStart();
    failures += TestPrecommitFailureMatrix();
    failures += TestCallbackRouterCollisionIsRecoverable();
    failures += TestPostCommitFaultStopsWithoutRecommit();
    return failures == 0 ? 0 : 1;
}
