#pragma once

#include <Windows.h>

namespace gc::input {

struct ForegroundApi {
    decltype(&GetForegroundWindow) get_foreground_window;
    decltype(&GetWindowThreadProcessId) get_window_thread_process_id;
    DWORD current_process_id;
};

[[nodiscard]] bool IsCurrentProcessForeground(
    const ForegroundApi& api) noexcept;

struct ForegroundTransition {
    bool changed{};
    bool clear_input{};
};

class ForegroundTransitionTracker {
public:
    [[nodiscard]] ForegroundTransition Update(bool foreground) noexcept;

private:
    bool foreground_{true};
};

} // namespace gc::input
