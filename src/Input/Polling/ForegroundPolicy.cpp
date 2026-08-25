#include "Input/Polling/ForegroundPolicy.h"

namespace gc::input {

bool IsCurrentProcessForeground(const ForegroundApi& api) noexcept
{
    if (api.get_foreground_window == nullptr ||
        api.get_window_thread_process_id == nullptr ||
        api.current_process_id == 0)
    {
        return false;
    }

    auto* const foreground = api.get_foreground_window();
    if (foreground == nullptr)
    {
        return false;
    }

    DWORD process_id = 0;
    if (api.get_window_thread_process_id(foreground, &process_id) == 0)
    {
        return false;
    }
    return process_id == api.current_process_id;
}

ForegroundTransition ForegroundTransitionTracker::Update(
    bool foreground) noexcept
{
    if (foreground == foreground_)
    {
        return {};
    }
    foreground_ = foreground;
    return ForegroundTransition{
        .changed = true,
        .clear_input = !foreground,
    };
}

} // namespace gc::input
