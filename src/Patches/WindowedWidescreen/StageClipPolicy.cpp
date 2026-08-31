#include "Patches/WindowedWidescreen/StageClipPolicy.h"

namespace gc::windowed_widescreen
{
    ClipGateAction SelectClipGateAction(const StageClipPolicy policy) noexcept
    {
        switch (policy)
        {
        case StageClipPolicy::authored:
            return ClipGateAction::continue_authored;
        case StageClipPolicy::live_frustum:
            return ClipGateAction::jump_live_frustum;
        }
        return ClipGateAction::continue_authored;
    }
} // namespace gc::windowed_widescreen
