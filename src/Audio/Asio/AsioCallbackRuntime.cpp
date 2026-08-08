// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioCallbackRuntime.h"

#include <avrt.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace gc::audio {
namespace {

std::atomic<AsioCallbackRuntime*> g_active_runtime{};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint8_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<AsioCallbackRuntime*>::is_always_lock_free);
static_assert(
    static_cast<unsigned>(AsioFailureStage::probe_crash) <=
    std::numeric_limits<std::uint8_t>::max());

bool ProductionQueryPerformanceCounter(
    void*,
    std::uint64_t* value) noexcept {
    LARGE_INTEGER counter{};
    if (value == nullptr || !QueryPerformanceCounter(&counter) ||
        counter.QuadPart < 0) {
        return false;
    }
    *value = static_cast<std::uint64_t>(counter.QuadPart);
    return true;
}

bool ProductionQueryPerformanceFrequency(
    void*,
    std::uint64_t* value) noexcept {
    LARGE_INTEGER frequency{};
    if (value == nullptr || !QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0) {
        return false;
    }
    *value = static_cast<std::uint64_t>(frequency.QuadPart);
    return true;
}

void* ProductionPromoteWorker(
    void*,
    const wchar_t* task_name,
    std::uint32_t* task_index) noexcept {
    return AvSetMmThreadCharacteristicsW(
        task_name,
        reinterpret_cast<DWORD*>(task_index));
}

bool ProductionRevertWorker(void*, void* registration) noexcept {
    return AvRevertMmThreadCharacteristics(
        static_cast<HANDLE>(registration)) != FALSE;
}

AsioFailure CallbackPrepareFailure(
    AsioResultDomain domain,
    std::int64_t result,
    const char* detail) {
    return {
        .stage = AsioFailureStage::callback_prepare,
        .domain = domain,
        .result = result,
        .detail = detail,
    };
}

template <typename Atomic>
void UpdateMaximum(Atomic& destination, std::uint64_t value) noexcept {
    auto observed = destination.load(std::memory_order_relaxed);
    while (observed < value &&
           !destination.compare_exchange_weak(
               observed,
               value,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

bool ConvertSamples(
    const ASIOSamples& source,
    std::uint64_t* value) noexcept {
    if (value == nullptr) {
        return false;
    }
#if NATIVE_INT64
    if (source < 0) {
        return false;
    }
    *value = static_cast<std::uint64_t>(source);
#else
    if ((source.hi & 0x80000000UL) != 0) {
        return false;
    }
    *value =
        (static_cast<std::uint64_t>(source.hi) << 32U) |
        static_cast<std::uint64_t>(source.lo);
#endif
    return true;
}

bool ConvertTimestamp(
    const ASIOTimeStamp& source,
    std::uint64_t* value) noexcept {
    if (value == nullptr) {
        return false;
    }
#if NATIVE_INT64
    if (source < 0) {
        return false;
    }
    *value = static_cast<std::uint64_t>(source);
#else
    if ((source.hi & 0x80000000UL) != 0) {
        return false;
    }
    *value =
        (static_cast<std::uint64_t>(source.hi) << 32U) |
        static_cast<std::uint64_t>(source.lo);
#endif
    return true;
}

bool IsSupportedSelector(long selector) noexcept {
    switch (selector) {
    case kAsioSelectorSupported:
    case kAsioEngineVersion:
    case kAsioResetRequest:
    case kAsioBufferSizeChange:
    case kAsioResyncRequest:
    case kAsioLatenciesChanged:
    case kAsioSupportsTimeInfo:
    case kAsioOverload:
        return true;
    default:
        return false;
    }
}

} // namespace

ASIOCallbacks AsioCallbackRuntime::callbacks_{
    &AsioCallbackRuntime::BufferSwitch,
    &AsioCallbackRuntime::SampleRateDidChange,
    &AsioCallbackRuntime::AsioMessage,
    &AsioCallbackRuntime::BufferSwitchTimeInfo,
};

AsioCallbackRuntimeActions
ProductionAsioCallbackRuntimeActions() noexcept {
    return {
        nullptr,
        &ProductionQueryPerformanceCounter,
        &ProductionQueryPerformanceFrequency,
        &ProductionPromoteWorker,
        &ProductionRevertWorker,
    };
}

AsioCallbackRuntime::AsioCallbackRuntime(
    IAsioBlockRenderer& renderer,
    AsioLegacyPositionActions legacy_actions,
    AsioCallbackRuntimeActions runtime_actions,
    std::uint64_t qpc_frequency) noexcept
    : renderer_(&renderer),
      legacy_actions_(legacy_actions),
      runtime_actions_(runtime_actions),
      qpc_frequency_(qpc_frequency) {}

std::expected<std::unique_ptr<AsioCallbackRuntime>, AsioFailure>
AsioCallbackRuntime::Prepare(
    IAsioBlockRenderer& renderer,
    AsioLegacyPositionActions legacy_actions,
    AsioCallbackRuntimeActions runtime_actions) noexcept {
    if (legacy_actions.get_sample_position == nullptr ||
        runtime_actions.query_performance_counter == nullptr ||
        runtime_actions.query_performance_frequency == nullptr ||
        runtime_actions.promote_worker == nullptr ||
        runtime_actions.revert_worker == nullptr) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::none,
            0,
            "ASIO callback actions are incomplete"));
    }

    std::uint64_t qpc_frequency{};
    if (!runtime_actions.query_performance_frequency(
            runtime_actions.context,
            &qpc_frequency) ||
        qpc_frequency == 0) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::win32,
            GetLastError(),
            "QueryPerformanceFrequency failed"));
    }

    auto runtime = std::unique_ptr<AsioCallbackRuntime>(
        new (std::nothrow) AsioCallbackRuntime(
            renderer,
            legacy_actions,
            runtime_actions,
            qpc_frequency));
    if (!runtime) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::none,
            0,
            "ASIO callback runtime allocation failed"));
    }

    auto worker = runtime->PrepareWorker();
    if (!worker.has_value()) {
        return std::unexpected(std::move(worker.error()));
    }
    return runtime;
}

std::expected<void, AsioFailure>
AsioCallbackRuntime::PrepareWorker() noexcept {
    work_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (work_event_ == nullptr) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::win32,
            GetLastError(),
            "CreateEventW(work) failed"));
    }
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event_ == nullptr) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::win32,
            GetLastError(),
            "CreateEventW(stop) failed"));
    }
    worker_ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (worker_ready_event_ == nullptr) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::win32,
            GetLastError(),
            "CreateEventW(worker ready) failed"));
    }

    worker_thread_ = CreateThread(
        nullptr,
        0,
        &AsioCallbackRuntime::WorkerEntry,
        this,
        0,
        nullptr);
    if (worker_thread_ == nullptr) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::win32,
            GetLastError(),
            "CreateThread(ASIO deferred worker) failed"));
    }
    if (WaitForSingleObject(worker_ready_event_, INFINITE) != WAIT_OBJECT_0) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::win32,
            GetLastError(),
            "waiting for ASIO deferred worker failed"));
    }
    if (!worker_ready_success_.load(std::memory_order_acquire)) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::win32,
            worker_prepare_error_.load(std::memory_order_acquire),
            "AvSetMmThreadCharacteristicsW(Pro Audio) failed"));
    }
    return {};
}

AsioCallbackRuntime::~AsioCallbackRuntime() {
    BeginStopping();
    JoinWorker();
    Uninstall();
    if (worker_ready_event_ != nullptr) {
        CloseHandle(worker_ready_event_);
    }
    if (stop_event_ != nullptr) {
        CloseHandle(stop_event_);
    }
    if (work_event_ != nullptr) {
        CloseHandle(work_event_);
    }
}

std::expected<void, AsioFailure>
AsioCallbackRuntime::Install() noexcept {
    if (joined_.load(std::memory_order_acquire) ||
        stopping_.load(std::memory_order_acquire) ||
        worker_thread_ == nullptr) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::none,
            0,
            "ASIO callback worker is not available"));
    }
    if (installed_.load(std::memory_order_acquire)) {
        return {};
    }

    AsioCallbackRuntime* expected{};
    if (!g_active_runtime.compare_exchange_strong(
            expected,
            this,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return std::unexpected(CallbackPrepareFailure(
            AsioResultDomain::none,
            0,
            "another ASIO callback runtime is active"));
    }
    installed_.store(true, std::memory_order_release);
    return {};
}

void AsioCallbackRuntime::BeginStopping() noexcept {
    stopping_.store(true, std::memory_order_release);
    if (stop_event_ != nullptr) {
        SetEvent(stop_event_);
    }
}

void AsioCallbackRuntime::JoinWorker() noexcept {
    if (joined_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    BeginStopping();
    if (worker_thread_ != nullptr) {
        WaitForSingleObject(worker_thread_, INFINITE);
        CloseHandle(worker_thread_);
        worker_thread_ = nullptr;
    }
}

void AsioCallbackRuntime::Uninstall() noexcept {
    AsioCallbackRuntime* expected = this;
    g_active_runtime.compare_exchange_strong(
        expected,
        nullptr,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
    installed_.store(false, std::memory_order_release);
}

ASIOCallbacks* AsioCallbackRuntime::Callbacks() noexcept {
    return &callbacks_;
}

DWORD WINAPI AsioCallbackRuntime::WorkerEntry(void* context) noexcept {
    return static_cast<AsioCallbackRuntime*>(context)->WorkerMain();
}

DWORD AsioCallbackRuntime::WorkerMain() noexcept {
    std::uint32_t task_index{};
    void* mmcss_registration = runtime_actions_.promote_worker(
        runtime_actions_.context,
        L"Pro Audio",
        &task_index);
    if (mmcss_registration == nullptr) {
        worker_prepare_error_.store(GetLastError(), std::memory_order_release);
        worker_ready_success_.store(false, std::memory_order_release);
        SetEvent(worker_ready_event_);
        return 1;
    }

    worker_ready_success_.store(true, std::memory_order_release);
    SetEvent(worker_ready_event_);
    const HANDLE events[]{stop_event_, work_event_};
    for (;;) {
        const auto wait = WaitForMultipleObjects(
            static_cast<DWORD>(std::size(events)),
            events,
            FALSE,
            INFINITE);
        if (wait == WAIT_OBJECT_0) {
            // A callback can have won the single-render claim immediately
            // before BeginStopping. Do not let the worker exit between that
            // claim and publication of its deferred slot.
            while (render_claimed_.load(std::memory_order_acquire)) {
                ProcessDeferredRequest();
                if (render_claimed_.load(std::memory_order_acquire)) {
                    WaitForSingleObject(work_event_, 1);
                }
            }
            break;
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            ProcessDeferredRequest();
            continue;
        }
        LatchFault(AsioFailureStage::callback);
        break;
    }

    runtime_actions_.revert_worker(
        runtime_actions_.context,
        mmcss_registration);
    return 0;
}

void AsioCallbackRuntime::ProcessDeferredRequest() noexcept {
    if (!deferred_pending_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const auto request = deferred_request_;
    if (stopping_.load(std::memory_order_acquire) || IsFaulted()) {
        renderer_->ClearAsioBlock(request.buffer_index);
        render_claimed_.store(false, std::memory_order_release);
        return;
    }

    std::uint64_t start{};
    const bool has_start = runtime_actions_.query_performance_counter(
        runtime_actions_.context,
        &start);
    renderer_->RenderAsioBlock(request);
    std::uint64_t finish{};
    if (has_start && runtime_actions_.query_performance_counter(
            runtime_actions_.context,
            &finish) &&
        finish >= start) {
        UpdateMaximum(maximum_render_ticks_, finish - start);
    }
    render_claimed_.store(false, std::memory_order_release);
}

void AsioCallbackRuntime::BufferSwitch(
    long buffer_index,
    ASIOBool direct_process) noexcept {
    auto* runtime = g_active_runtime.load(std::memory_order_acquire);
    if (runtime != nullptr) {
        runtime->DispatchLegacy(buffer_index, direct_process);
    }
}

void AsioCallbackRuntime::SampleRateDidChange(
    ASIOSampleRate sample_rate) noexcept {
    auto* runtime = g_active_runtime.load(std::memory_order_acquire);
    if (runtime != nullptr) {
        runtime->RecordSampleRateChange(sample_rate);
        runtime->LatchFault(AsioFailureStage::runtime_clock);
    }
}

long AsioCallbackRuntime::AsioMessage(
    long selector,
    long value,
    void*,
    double*) noexcept {
    auto* runtime = g_active_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr) {
        return 0;
    }

    switch (selector) {
    case kAsioSelectorSupported:
        return IsSupportedSelector(value) ? 1 : 0;
    case kAsioEngineVersion:
        return 2;
    case kAsioSupportsTimeInfo:
        return 1;
    case kAsioSupportsTimeCode:
        return 0;
    case kAsioResetRequest:
        ++runtime->reset_requests_;
        runtime->LatchFault(AsioFailureStage::callback);
        return 1;
    case kAsioBufferSizeChange:
        ++runtime->buffer_size_change_requests_;
        runtime->LatchFault(AsioFailureStage::callback);
        return 0;
    case kAsioResyncRequest:
        ++runtime->resync_requests_;
        runtime->LatchFault(AsioFailureStage::callback);
        return 1;
    case kAsioLatenciesChanged:
        ++runtime->latency_change_requests_;
        runtime->LatchFault(AsioFailureStage::callback);
        return 1;
    case kAsioOverload:
        ++runtime->overload_messages_;
        runtime->LatchFault(AsioFailureStage::callback);
        return 1;
    default:
        return 0;
    }
}

ASIOTime* AsioCallbackRuntime::BufferSwitchTimeInfo(
    ASIOTime* time,
    long buffer_index,
    ASIOBool direct_process) noexcept {
    auto* runtime = g_active_runtime.load(std::memory_order_acquire);
    if (runtime != nullptr) {
        runtime->DispatchTimeInfo(time, buffer_index, direct_process);
    }
    return time;
}

void AsioCallbackRuntime::DispatchTimeInfo(
    ASIOTime* time,
    long buffer_index,
    ASIOBool direct_process) noexcept {
    std::uint64_t start{};
    const bool has_start = runtime_actions_.query_performance_counter(
        runtime_actions_.context,
        &start);
    ++callbacks_count_;
    ++time_info_callbacks_;
    if (direct_process == ASIOFalse) {
        ++deferred_callbacks_;
    }

    AsioRenderRequest request{
        buffer_index,
        direct_process,
        true,
        0,
        0,
    };
    auto validation_failure = AsioFailureStage::none;
    if (time == nullptr ||
        (time->timeInfo.flags & kSystemTimeValid) == 0 ||
        (time->timeInfo.flags & kSamplePositionValid) == 0 ||
        !ConvertSamples(
            time->timeInfo.samplePosition,
            &request.sample_position) ||
        !ConvertTimestamp(
            time->timeInfo.systemTime,
            &request.system_time_ns)) {
        validation_failure = AsioFailureStage::runtime_clock;
    } else if ((time->timeInfo.flags & kSampleRateChanged) != 0) {
        RecordSampleRateChange(time->timeInfo.sampleRate);
        validation_failure = AsioFailureStage::runtime_clock;
    } else if ((time->timeInfo.flags & kClockSourceChanged) != 0) {
        validation_failure = AsioFailureStage::runtime_clock;
    } else if ((time->timeInfo.flags & kSampleRateValid) != 0 &&
               (!std::isfinite(time->timeInfo.sampleRate) ||
                time->timeInfo.sampleRate != 48'000.0)) {
        RecordSampleRateChange(time->timeInfo.sampleRate);
        validation_failure = AsioFailureStage::runtime_clock;
    } else if ((time->timeInfo.flags & kSpeedValid) != 0 &&
               (!std::isfinite(time->timeInfo.speed) ||
                time->timeInfo.speed != 1.0)) {
        validation_failure = AsioFailureStage::runtime_clock;
    }

    DispatchValidated(request, validation_failure);
    FinishCallbackTiming(has_start, start);
}

void AsioCallbackRuntime::DispatchLegacy(
    long buffer_index,
    ASIOBool direct_process) noexcept {
    std::uint64_t start{};
    const bool has_start = runtime_actions_.query_performance_counter(
        runtime_actions_.context,
        &start);
    ++callbacks_count_;
    ++legacy_callbacks_;
    if (direct_process == ASIOFalse) {
        ++deferred_callbacks_;
    }

    ASIOSamples samples{};
    ASIOTimeStamp timestamp{};
    AsioRenderRequest request{
        buffer_index,
        direct_process,
        false,
        0,
        0,
    };
    auto validation_failure = AsioFailureStage::none;
    const auto result = legacy_actions_.get_sample_position(
        legacy_actions_.context,
        &samples,
        &timestamp);
    if (result != ASE_OK && result != ASE_SUCCESS) {
        validation_failure = AsioFailureStage::callback;
    } else if (!ConvertSamples(samples, &request.sample_position) ||
               !ConvertTimestamp(timestamp, &request.system_time_ns)) {
        validation_failure = AsioFailureStage::runtime_clock;
    }

    DispatchValidated(request, validation_failure);
    FinishCallbackTiming(has_start, start);
}

void AsioCallbackRuntime::DispatchValidated(
    const AsioRenderRequest& request,
    AsioFailureStage validation_failure) noexcept {
    if (request.buffer_index < 0 || request.buffer_index > 1) {
        LatchFault(AsioFailureStage::callback);
        return;
    }

    bool expected{};
    if (!render_claimed_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        ++deadline_misses_;
        LatchFault(AsioFailureStage::callback);
        return;
    }

    if (stopping_.load(std::memory_order_acquire) || IsFaulted()) {
        renderer_->ClearAsioBlock(request.buffer_index);
        render_claimed_.store(false, std::memory_order_release);
        return;
    }
    if (request.direct_process != ASIOTrue &&
        request.direct_process != ASIOFalse) {
        renderer_->ClearAsioBlock(request.buffer_index);
        LatchFault(AsioFailureStage::callback);
        render_claimed_.store(false, std::memory_order_release);
        return;
    }
    if (previous_buffer_index_ == request.buffer_index) {
        ++buffer_alternation_violations_;
        renderer_->ClearAsioBlock(request.buffer_index);
        LatchFault(AsioFailureStage::callback);
        render_claimed_.store(false, std::memory_order_release);
        return;
    }
    previous_buffer_index_ = request.buffer_index;

    if (validation_failure != AsioFailureStage::none) {
        renderer_->ClearAsioBlock(request.buffer_index);
        LatchFault(validation_failure);
        render_claimed_.store(false, std::memory_order_release);
        return;
    }
    if (request.direct_process == ASIOTrue) {
        renderer_->RenderAsioBlock(request);
        render_claimed_.store(false, std::memory_order_release);
        return;
    }

    deferred_request_ = request;
    deferred_pending_.store(true, std::memory_order_release);
    if (!SetEvent(work_event_)) {
        deferred_pending_.store(false, std::memory_order_release);
        renderer_->ClearAsioBlock(request.buffer_index);
        LatchFault(AsioFailureStage::callback);
        render_claimed_.store(false, std::memory_order_release);
    }
}

void AsioCallbackRuntime::FinishCallbackTiming(
    bool has_start,
    std::uint64_t start_tick) noexcept {
    std::uint64_t finish{};
    if (has_start && runtime_actions_.query_performance_counter(
            runtime_actions_.context,
            &finish) &&
        finish >= start_tick) {
        UpdateMaximum(maximum_callback_ticks_, finish - start_tick);
    }
}

void AsioCallbackRuntime::LatchFault(AsioFailureStage stage) noexcept {
    if (stage == AsioFailureStage::none) {
        return;
    }
    std::uint8_t expected =
        static_cast<std::uint8_t>(AsioFailureStage::none);
    if (first_fault_.compare_exchange_strong(
            expected,
            static_cast<std::uint8_t>(stage),
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        renderer_->OnAsioRuntimeFault(stage);
    }
}

void AsioCallbackRuntime::RecordSampleRateChange(
    double sample_rate) noexcept {
    ++sample_rate_change_requests_;
    last_reported_sample_rate_bits_.store(
        std::bit_cast<std::uint64_t>(sample_rate),
        std::memory_order_release);
}

bool AsioCallbackRuntime::IsFaulted() const noexcept {
    return first_fault_.load(std::memory_order_acquire) !=
        static_cast<std::uint8_t>(AsioFailureStage::none);
}

AsioCallbackRuntimeSnapshot
AsioCallbackRuntime::Snapshot() const noexcept {
    return {
        .callbacks = callbacks_count_.load(std::memory_order_acquire),
        .time_info_callbacks =
            time_info_callbacks_.load(std::memory_order_acquire),
        .legacy_callbacks =
            legacy_callbacks_.load(std::memory_order_acquire),
        .deferred_callbacks =
            deferred_callbacks_.load(std::memory_order_acquire),
        .deadline_misses =
            deadline_misses_.load(std::memory_order_acquire),
        .overload_messages =
            overload_messages_.load(std::memory_order_acquire),
        .reset_requests =
            reset_requests_.load(std::memory_order_acquire),
        .resync_requests =
            resync_requests_.load(std::memory_order_acquire),
        .latency_change_requests =
            latency_change_requests_.load(std::memory_order_acquire),
        .buffer_size_change_requests =
            buffer_size_change_requests_.load(std::memory_order_acquire),
        .sample_rate_change_requests =
            sample_rate_change_requests_.load(std::memory_order_acquire),
        .buffer_alternation_violations =
            buffer_alternation_violations_.load(std::memory_order_acquire),
        .maximum_callback_ticks =
            maximum_callback_ticks_.load(std::memory_order_acquire),
        .maximum_render_ticks =
            maximum_render_ticks_.load(std::memory_order_acquire),
        .qpc_frequency = qpc_frequency_,
        .last_reported_sample_rate = std::bit_cast<double>(
            last_reported_sample_rate_bits_.load(
                std::memory_order_acquire)),
        .first_fault = static_cast<AsioFailureStage>(
            first_fault_.load(std::memory_order_acquire)),
    };
}

} // namespace gc::audio
