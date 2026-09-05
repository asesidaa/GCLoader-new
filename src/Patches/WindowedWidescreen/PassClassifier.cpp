#include "Patches/WindowedWidescreen/PassClassifier.h"

#include <algorithm>
#include <limits>

namespace gc::windowed_widescreen
{
    PassClassifier::PassClassifier(
        std::uintptr_t common_2d_vtable, std::uintptr_t common_3d_vtable,
        PassClassifierDiagnosticSink diagnostics) noexcept
        : common_2d_vtable_{common_2d_vtable}, common_3d_vtable_{common_3d_vtable},
          diagnostics_{diagnostics} {}

    RenderSpace PassClassifier::ClassifyTask(
        const std::uintptr_t task_vtable) noexcept
    {
        if (common_2d_vtable_ != 0 && task_vtable == common_2d_vtable_)
        {
            return RenderSpace::native_2d;
        }
        if (common_3d_vtable_ != 0 && task_vtable == common_3d_vtable_)
        {
            return RenderSpace::physical_3d;
        }

        RecordUnknown(task_vtable);
        return RenderSpace::native_2d;
    }

    RenderSpace PassClassifier::ClassifyGameplay(
        const GameplayPass pass) noexcept
    {
        switch (pass)
        {
        case GameplayPass::orthographic_effects:
            return RenderSpace::gameplay_hud;
        case GameplayPass::stage_background:
        case GameplayPass::perspective_track:
            return RenderSpace::physical_3d;
        }
        return RenderSpace::native_2d;
    }

    void PassClassifier::RecordUnknown(
        const std::uintptr_t task_vtable) noexcept
    {
        if (task_vtable == 0)
        {
            return;
        }

        const auto populated_end = unknown_identities_.begin() +
            static_cast<std::ptrdiff_t>(unknown_identity_count_);
        const auto existing = std::find(
            unknown_identities_.begin(),
            populated_end,
            task_vtable);
        if (existing != populated_end)
        {
            return;
        }

        if (unknown_identity_count_ < unknown_identities_.size())
        {
            unknown_identities_[unknown_identity_count_++] = task_vtable;
            if (diagnostics_.unknown_identity != nullptr)
            {
                diagnostics_.unknown_identity(
                    diagnostics_.context,
                    task_vtable);
            }
            return;
        }

        if (!unknown_capacity_exhausted_)
        {
            unknown_capacity_exhausted_ = true;
            if (diagnostics_.unknown_capacity_exhausted != nullptr)
            {
                diagnostics_.unknown_capacity_exhausted(
                    diagnostics_.context);
            }
        }
    }
} // namespace gc::windowed_widescreen
