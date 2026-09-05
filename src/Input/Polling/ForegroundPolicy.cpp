#include "Input/Polling/ForegroundPolicy.h"

namespace gc::input {

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
