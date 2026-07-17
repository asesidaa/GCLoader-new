#pragma once

#include "Rfid/ComPortState.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <expected>
#include <mutex>

namespace gc::rfid {

using CardWorkerEntry = void (*)(void*) noexcept;

struct CardWorkerApi {
    std::expected<void, DWORD> (*start_detached)(
        CardWorkerEntry entry, void* context) noexcept;
    SHORT (*get_async_key_state)(int virtual_key) noexcept;
    void (*sleep_for)(std::chrono::milliseconds duration) noexcept;
};

[[nodiscard]] CardWorkerApi ProductionCardWorkerApi() noexcept;
[[nodiscard]] HANDLE EmulatedComHandle() noexcept;

class Runtime {
public:
    explicit Runtime(
        int card_virtual_key,
        CardWorkerApi worker_api = ProductionCardWorkerApi()) noexcept;

    [[nodiscard]] std::expected<HANDLE, DWORD> OpenCom2() noexcept;
    void CloseCom2() noexcept;
    [[nodiscard]] ComPortState& port() noexcept;

private:
    static void CardWorkerMain(void* context) noexcept;
    void RunCardWorker() noexcept;

    int card_virtual_key_{};
    CardWorkerApi worker_api_{};
    ComPortState port_{};
    std::once_flag worker_once_;
    std::atomic_bool worker_started_{};
    std::atomic<DWORD> worker_error_{ERROR_SUCCESS};
};

} // namespace gc::rfid
