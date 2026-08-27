#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioForegroundState.h"
#include "Audio/Asio/AsioTypes.h"

#include <Windows.h>

#include <atomic>
#include <memory>
#include <thread>

namespace gc::audio
{
    class AsioForegroundMonitor final
    {
    public:
        [[nodiscard]] static std::unique_ptr<AsioForegroundMonitor> Start(
            HWND game_window,
            AsioFailure* failure) noexcept;

        ~AsioForegroundMonitor();

        AsioForegroundMonitor(const AsioForegroundMonitor&) = delete;
        AsioForegroundMonitor& operator=(const AsioForegroundMonitor&) = delete;

        [[nodiscard]] HANDLE change_event() const noexcept;
        [[nodiscard]] AsioForegroundSnapshot snapshot() const noexcept;
        [[nodiscard]] bool healthy() const noexcept;
        [[nodiscard]] DWORD failure_code() const noexcept;

    private:
        explicit AsioForegroundMonitor(DWORD game_process_id) noexcept;

        [[nodiscard]] bool CreateEvents(AsioFailure* failure) noexcept;
        [[nodiscard]] bool StartThread(AsioFailure* failure) noexcept;
        void ShutdownAndJoin() noexcept;
        void CloseEvents() noexcept;
        void ThreadMain() noexcept;
        [[nodiscard]] bool QueryForegroundWindow() const noexcept;
        void PublishCurrentForeground() noexcept;
        void PublishForeground(bool foreground) noexcept;
        void PublishThreadFailure(DWORD error) noexcept;

        static void CALLBACK WinEventCallback(
            HWINEVENTHOOK,
            DWORD event,
            HWND,
            LONG,
            LONG,
            DWORD,
            DWORD) noexcept;

        static thread_local AsioForegroundMonitor* callback_owner_;

        DWORD game_process_id_{};
        std::thread thread_;
        HANDLE ready_event_{};
        HANDLE change_event_{};
        HANDLE shutdown_event_{};
        std::atomic<DWORD> thread_id_{};
        std::atomic_bool startup_succeeded_{};
        AsioForegroundState foreground_state_;
        std::atomic_bool healthy_{};
        std::atomic<DWORD> failure_code_{};
    };
} // namespace gc::audio
