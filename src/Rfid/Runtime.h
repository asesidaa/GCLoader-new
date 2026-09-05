#pragma once

#include "Rfid/ComPortState.h"

#include <Windows.h>

#include <atomic>
#include <expected>
#include <mutex>

namespace gc::rfid {

[[nodiscard]] HANDLE EmulatedComHandle() noexcept;

class Runtime {
public:
    explicit Runtime(int card_virtual_key) noexcept;

    [[nodiscard]] std::expected<HANDLE, DWORD> OpenCom2() noexcept;
    void CloseCom2() noexcept;
    [[nodiscard]] ComPortState& port() noexcept;

private:
    static void CardWorkerMain(void* context) noexcept;
    static void CardReaderWorkerMain(void* context) noexcept;
    void RunCardWorker() noexcept;
    void RunCardReaderWorker() noexcept;

    int card_virtual_key_{};
    ComPortState port_{};
    std::once_flag worker_once_;
    std::once_flag card_reader_worker_once_;
    std::atomic_bool worker_started_{};
    std::atomic<DWORD> worker_error_{ERROR_SUCCESS};
};

} // namespace gc::rfid
