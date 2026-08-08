#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioTypes.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>

namespace gc::audio {

struct AsioRenderRequest {
    long buffer_index{};
    ASIOBool direct_process{};
    bool has_time_info{};
    std::uint64_t sample_position{};
    std::uint64_t system_time_ns{};
};

class IAsioBlockRenderer {
public:
    virtual ~IAsioBlockRenderer() = default;
    virtual void RenderAsioBlock(
        const AsioRenderRequest& request) noexcept = 0;
    virtual void ClearAsioBlock(long buffer_index) noexcept = 0;
    virtual void OnAsioRuntimeFault(
        AsioFailureStage stage) noexcept = 0;
};

struct AsioLegacyPositionActions {
    void* context{};
    ASIOError (*get_sample_position)(
        void* context,
        ASIOSamples* sample_position,
        ASIOTimeStamp* timestamp) noexcept{};
};

struct AsioCallbackRuntimeActions {
    void* context{};
    bool (*query_performance_counter)(
        void* context,
        std::uint64_t* value) noexcept{};
    bool (*query_performance_frequency)(
        void* context,
        std::uint64_t* value) noexcept{};
    void* (*promote_worker)(
        void* context,
        const wchar_t* task_name,
        std::uint32_t* task_index) noexcept{};
    bool (*revert_worker)(
        void* context,
        void* registration) noexcept{};
};

[[nodiscard]] AsioCallbackRuntimeActions
ProductionAsioCallbackRuntimeActions() noexcept;

struct AsioCallbackRuntimeSnapshot {
    std::uint64_t callbacks{};
    std::uint64_t time_info_callbacks{};
    std::uint64_t legacy_callbacks{};
    std::uint64_t deferred_callbacks{};
    std::uint64_t deadline_misses{};
    std::uint64_t overload_messages{};
    std::uint64_t reset_requests{};
    std::uint64_t resync_requests{};
    std::uint64_t latency_change_requests{};
    std::uint64_t buffer_size_change_requests{};
    std::uint64_t sample_rate_change_requests{};
    std::uint64_t buffer_alternation_violations{};
    std::uint64_t maximum_callback_ticks{};
    std::uint64_t maximum_render_ticks{};
    std::uint64_t qpc_frequency{};
    double last_reported_sample_rate{};
    AsioFailureStage first_fault{AsioFailureStage::none};
};

class AsioCallbackRuntime final {
public:
    static std::expected<std::unique_ptr<AsioCallbackRuntime>, AsioFailure>
    Prepare(
        IAsioBlockRenderer& renderer,
        AsioLegacyPositionActions legacy_actions,
        AsioCallbackRuntimeActions runtime_actions =
            ProductionAsioCallbackRuntimeActions()) noexcept;

    ~AsioCallbackRuntime();
    AsioCallbackRuntime(const AsioCallbackRuntime&) = delete;
    AsioCallbackRuntime& operator=(const AsioCallbackRuntime&) = delete;

    [[nodiscard]] std::expected<void, AsioFailure> Install() noexcept;
    void BeginStopping() noexcept;
    void JoinWorker() noexcept;
    void Uninstall() noexcept;

    [[nodiscard]] AsioCallbackRuntimeSnapshot Snapshot() const noexcept;
    [[nodiscard]] static ASIOCallbacks* Callbacks() noexcept;

private:
    AsioCallbackRuntime(
        IAsioBlockRenderer& renderer,
        AsioLegacyPositionActions legacy_actions,
        AsioCallbackRuntimeActions runtime_actions,
        std::uint64_t qpc_frequency) noexcept;

    [[nodiscard]] std::expected<void, AsioFailure>
        PrepareWorker() noexcept;
    static DWORD WINAPI WorkerEntry(void* context) noexcept;
    DWORD WorkerMain() noexcept;
    void ProcessDeferredRequest() noexcept;

    static void BufferSwitch(
        long buffer_index,
        ASIOBool direct_process) noexcept;
    static void SampleRateDidChange(ASIOSampleRate sample_rate) noexcept;
    static long AsioMessage(
        long selector,
        long value,
        void* message,
        double* option) noexcept;
    static ASIOTime* BufferSwitchTimeInfo(
        ASIOTime* time,
        long buffer_index,
        ASIOBool direct_process) noexcept;

    void DispatchTimeInfo(
        ASIOTime* time,
        long buffer_index,
        ASIOBool direct_process) noexcept;
    void DispatchLegacy(
        long buffer_index,
        ASIOBool direct_process) noexcept;
    [[nodiscard]] bool DispatchValidated(
        const AsioRenderRequest& request,
        AsioFailureStage validation_failure) noexcept;
    void FinishCallbackTiming(
        bool has_start,
        std::uint64_t start_tick,
        bool rendered_inline) noexcept;
    void LatchFault(AsioFailureStage stage) noexcept;
    void RecordSampleRateChange(double sample_rate) noexcept;
    [[nodiscard]] bool IsFaulted() const noexcept;

    static ASIOCallbacks callbacks_;

    IAsioBlockRenderer* renderer_{};
    AsioLegacyPositionActions legacy_actions_{};
    AsioCallbackRuntimeActions runtime_actions_{};
    std::uint64_t qpc_frequency_{};

    HANDLE work_event_{};
    HANDLE stop_event_{};
    HANDLE worker_ready_event_{};
    HANDLE worker_thread_{};
    std::atomic_bool worker_ready_success_{};
    std::atomic<DWORD> worker_prepare_error_{};

    std::atomic_bool stopping_{};
    std::atomic_bool installed_{};
    std::atomic_bool joined_{};
    std::atomic_bool render_claimed_{};
    std::atomic_bool deferred_pending_{};
    AsioRenderRequest deferred_request_{};
    long previous_buffer_index_{-1};

    std::atomic_uint64_t callbacks_count_{};
    std::atomic_uint64_t time_info_callbacks_{};
    std::atomic_uint64_t legacy_callbacks_{};
    std::atomic_uint64_t deferred_callbacks_{};
    std::atomic_uint64_t deadline_misses_{};
    std::atomic_uint64_t overload_messages_{};
    std::atomic_uint64_t reset_requests_{};
    std::atomic_uint64_t resync_requests_{};
    std::atomic_uint64_t latency_change_requests_{};
    std::atomic_uint64_t buffer_size_change_requests_{};
    std::atomic_uint64_t sample_rate_change_requests_{};
    std::atomic_uint64_t buffer_alternation_violations_{};
    std::atomic_uint64_t maximum_callback_ticks_{};
    std::atomic_uint64_t maximum_render_ticks_{};
    std::atomic_uint64_t last_reported_sample_rate_bits_{};
    std::atomic<std::uint8_t> first_fault_{};
};

} // namespace gc::audio
