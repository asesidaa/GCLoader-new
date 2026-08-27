// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioForegroundMonitor.h"

#include <new>
#include <string>
#include <string_view>

namespace gc::audio
{
    namespace
    {
        void SetFailure(
            AsioFailure* failure,
            const DWORD error,
            const std::string_view detail) noexcept
        {
            if (failure == nullptr)
            {
                return;
            }
            try
            {
                *failure = {
                    .stage = AsioFailureStage::foreground_monitor,
                    .domain = AsioResultDomain::win32,
                    .result = error,
                    .detail = std::string{detail},
                };
            }
            catch (...)
            {
                *failure = {
                    .stage = AsioFailureStage::foreground_monitor,
                    .domain = AsioResultDomain::win32,
                    .result = error,
                };
            }
        }
    } // namespace

    thread_local AsioForegroundMonitor*
        AsioForegroundMonitor::callback_owner_{};

    AsioForegroundMonitor::AsioForegroundMonitor(
        const DWORD game_process_id) noexcept
        : game_process_id_(game_process_id)
    {
    }

    AsioForegroundMonitor::~AsioForegroundMonitor()
    {
        ShutdownAndJoin();
        CloseEvents();
    }

    std::unique_ptr<AsioForegroundMonitor> AsioForegroundMonitor::Start(
        const HWND game_window,
        AsioFailure* failure) noexcept
    {
        if (failure != nullptr)
        {
            *failure = {};
        }
        if (game_window == nullptr)
        {
            SetFailure(
                failure,
                ERROR_INVALID_WINDOW_HANDLE,
                "ASIO foreground monitoring requires the game window");
            return nullptr;
        }

        DWORD game_process_id{};
        if (GetWindowThreadProcessId(game_window, &game_process_id) == 0 ||
            game_process_id == 0)
        {
            SetFailure(
                failure,
                GetLastError(),
                "Could not resolve the game process for ASIO foreground monitoring");
            return nullptr;
        }
        if (game_process_id != GetCurrentProcessId())
        {
            SetFailure(
                failure,
                ERROR_INVALID_OWNER,
                "ASIO foreground monitoring requires a window owned by this process");
            return nullptr;
        }

        auto monitor = std::unique_ptr<AsioForegroundMonitor>(
            new(std::nothrow) AsioForegroundMonitor(game_process_id));
        if (monitor == nullptr)
        {
            SetFailure(
                failure,
                ERROR_OUTOFMEMORY,
                "Could not allocate the ASIO foreground monitor");
            return nullptr;
        }
        if (!monitor->CreateEvents(failure) || !monitor->StartThread(failure))
        {
            return nullptr;
        }
        return monitor;
    }

    bool AsioForegroundMonitor::CreateEvents(AsioFailure* failure) noexcept
    {
        ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        change_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        shutdown_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ready_event_ != nullptr && change_event_ != nullptr &&
            shutdown_event_ != nullptr)
        {
            return true;
        }
        SetFailure(
            failure,
            GetLastError(),
            "Could not create ASIO foreground-monitor events");
        return false;
    }

    bool AsioForegroundMonitor::StartThread(AsioFailure* failure) noexcept
    {
        try
        {
            thread_ = std::thread([this]
            {
                ThreadMain();
            });
        }
        catch (...)
        {
            SetFailure(
                failure,
                ERROR_NOT_ENOUGH_MEMORY,
                "Could not create the ASIO foreground-monitor thread");
            return false;
        }

        const DWORD wait = WaitForSingleObject(ready_event_, INFINITE);
        if (wait == WAIT_OBJECT_0 &&
            startup_succeeded_.load(std::memory_order_acquire))
        {
            return true;
        }

        const DWORD error = wait == WAIT_FAILED
                                ? GetLastError()
                                : failure_code_.load(std::memory_order_acquire);
        SetFailure(
            failure,
            error,
            "ASIO foreground-monitor startup failed");
        ShutdownAndJoin();
        return false;
    }

    void AsioForegroundMonitor::ShutdownAndJoin() noexcept
    {
        if (shutdown_event_ != nullptr)
        {
            SetEvent(shutdown_event_);
        }
        const DWORD thread_id = thread_id_.load(std::memory_order_acquire);
        if (thread_id != 0)
        {
            PostThreadMessageW(thread_id, WM_NULL, 0, 0);
        }
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    void AsioForegroundMonitor::CloseEvents() noexcept
    {
        const HANDLE events[]{ready_event_, change_event_, shutdown_event_};
        for (const HANDLE event : events)
        {
            if (event != nullptr)
            {
                CloseHandle(event);
            }
        }
        ready_event_ = nullptr;
        change_event_ = nullptr;
        shutdown_event_ = nullptr;
    }

    void AsioForegroundMonitor::ThreadMain() noexcept
    {
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
        callback_owner_ = this;

        const HWINEVENTHOOK hook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_FOREGROUND,
            nullptr,
            &AsioForegroundMonitor::WinEventCallback,
            0,
            0,
            WINEVENT_OUTOFCONTEXT);
        if (hook == nullptr)
        {
            callback_owner_ = nullptr;
            PublishThreadFailure(GetLastError());
            SetEvent(ready_event_);
            return;
        }

        PublishCurrentForeground();
        healthy_.store(true, std::memory_order_release);
        startup_succeeded_.store(true, std::memory_order_release);
        SetEvent(ready_event_);

        const HANDLE handles[]{shutdown_event_};
        for (;;)
        {
            const DWORD wait = MsgWaitForMultipleObjectsEx(
                1,
                handles,
                INFINITE,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
            if (wait == WAIT_OBJECT_0)
            {
                break;
            }
            if (wait != WAIT_OBJECT_0 + 1)
            {
                PublishThreadFailure(
                    wait == WAIT_FAILED ? GetLastError() : wait);
                break;
            }

            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    PublishThreadFailure(ERROR_CANCELLED);
                    SetEvent(shutdown_event_);
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        UnhookWinEvent(hook);
        callback_owner_ = nullptr;
        healthy_.store(false, std::memory_order_release);
    }

    void AsioForegroundMonitor::PublishCurrentForeground() noexcept
    {
        PublishForeground(QueryForegroundWindow());
    }

    void AsioForegroundMonitor::PublishForeground(
        const bool foreground) noexcept
    {
        const auto result = foreground_state_.Publish(foreground);
        if (result == AsioForegroundPublishResult::generation_overflow)
        {
            PublishThreadFailure(ERROR_ARITHMETIC_OVERFLOW);
            return;
        }
        if (result == AsioForegroundPublishResult::changed &&
            change_event_ != nullptr)
        {
            SetEvent(change_event_);
        }
    }

    void AsioForegroundMonitor::PublishThreadFailure(const DWORD error) noexcept
    {
        failure_code_.store(error, std::memory_order_release);
        healthy_.store(false, std::memory_order_release);
        if (change_event_ != nullptr)
        {
            SetEvent(change_event_);
        }
    }

    void CALLBACK AsioForegroundMonitor::WinEventCallback(
        HWINEVENTHOOK,
        const DWORD event,
        HWND,
        LONG,
        LONG,
        DWORD,
        DWORD) noexcept
    {
        if (event == EVENT_SYSTEM_FOREGROUND && callback_owner_ != nullptr)
        {
            callback_owner_->PublishCurrentForeground();
        }
    }

    HANDLE AsioForegroundMonitor::change_event() const noexcept
    {
        return change_event_;
    }

    bool AsioForegroundMonitor::QueryForegroundWindow() const noexcept
    {
        const HWND foreground = GetForegroundWindow();
        if (foreground == nullptr)
        {
            return false;
        }
        DWORD process_id{};
        return GetWindowThreadProcessId(foreground, &process_id) != 0 &&
            process_id == game_process_id_;
    }

    AsioForegroundSnapshot AsioForegroundMonitor::snapshot() const noexcept
    {
        return foreground_state_.Read();
    }

    bool AsioForegroundMonitor::healthy() const noexcept
    {
        return healthy_.load(std::memory_order_acquire);
    }

    DWORD AsioForegroundMonitor::failure_code() const noexcept
    {
        return failure_code_.load(std::memory_order_acquire);
    }
} // namespace gc::audio
