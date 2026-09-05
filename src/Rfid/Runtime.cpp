#include "Rfid/Runtime.h"

#include "Rfid/CardReaderInterface.h"
#include "Rfid/CardReaderProtocol.h"

#include "plog/Log.h"

#include <chrono>
#include <cstdint>
#include <system_error>
#include <thread>

namespace gc::rfid {
namespace {

std::expected<void, DWORD> StartDetached(
    void (*entry)(void*) noexcept,
    void* context) noexcept
{
    try {
        std::thread{[entry, context] { entry(context); }}.detach();
        return {};
    } catch (const std::system_error&) {
        return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
    }
}

} // namespace

HANDLE EmulatedComHandle() noexcept
{
    return reinterpret_cast<HANDLE>(std::uintptr_t{0x1337});
}

Runtime::Runtime(int card_virtual_key) noexcept
    : card_virtual_key_{card_virtual_key}
{
}

std::expected<HANDLE, DWORD> Runtime::OpenCom2() noexcept
{
    std::call_once(worker_once_, [this] {
        const auto started = StartDetached(
            CardWorkerMain, this);
        if (started) {
            worker_started_.store(true, std::memory_order_release);
            return;
        }
        worker_error_.store(started.error(), std::memory_order_release);
    });

    if (!worker_started_.load(std::memory_order_acquire)) {
        const auto error = worker_error_.load(std::memory_order_acquire);
        return std::unexpected(
            error == ERROR_SUCCESS ? ERROR_NOT_ENOUGH_MEMORY : error);
    }

    std::call_once(card_reader_worker_once_, [this] {
        const auto started = StartDetached(
            CardReaderWorkerMain, this);
        if (!started) {
            try {
                PLOG_WARNING
                    << "RFID card-reader listener thread unavailable error="
                    << started.error();
            } catch (...) {
            }
        }
    });

    port_.Open();
    return EmulatedComHandle();
}

void Runtime::CloseCom2() noexcept
{
    port_.Close();
}

ComPortState& Runtime::port() noexcept
{
    return port_;
}

void Runtime::CardWorkerMain(void* context) noexcept
{
    static_cast<Runtime*>(context)->RunCardWorker();
}

void Runtime::CardReaderWorkerMain(void* context) noexcept
{
    static_cast<Runtime*>(context)->RunCardReaderWorker();
}

void Runtime::RunCardWorker() noexcept
{
    bool key_was_down = false;
    for (;;) {
        const bool key_is_down =
            card_virtual_key_ != 0 &&
            (::GetAsyncKeyState(card_virtual_key_) & 0x8000) != 0;
        if (key_is_down && !key_was_down) {
            port_.device_state().card_scan.Arm();
        }
        key_was_down = key_is_down;
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
}

void Runtime::RunCardReaderWorker() noexcept
{
    bool infrastructure_error_logged = false;
    for (;;) {
        const auto served =
            card_reader::ServeOneCardReaderConnection(
                card_reader::kPipeName,
                port_.device_state().card_scan);
        if (served) {
            infrastructure_error_logged = false;
            continue;
        }

        if (!infrastructure_error_logged) {
            try {
                PLOG_WARNING
                    << "RFID card-reader pipe unavailable error="
                    << served.error();
            } catch (...) {
            }
            infrastructure_error_logged = true;
        }
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
}

} // namespace gc::rfid
