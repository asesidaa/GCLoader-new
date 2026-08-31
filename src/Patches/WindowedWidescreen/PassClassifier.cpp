#include "Patches/WindowedWidescreen/PassClassifier.h"

#include <algorithm>
#include <limits>

namespace gc::windowed_widescreen
{
    namespace
    {
        constexpr std::uintptr_t kCommon2DVtableRva = 0x002F9AFC;
        constexpr std::uintptr_t kCommon3DVtableRva = 0x002FB218;

        [[nodiscard]] std::uintptr_t RelocateOrZero(
            const std::uintptr_t image_base,
            const std::uintptr_t rva) noexcept
        {
            if (image_base >
                std::numeric_limits<std::uintptr_t>::max() - rva)
            {
                return 0;
            }
            return image_base + rva;
        }
    } // namespace

    PassClassifier::PassClassifier(const std::uintptr_t image_base) noexcept
        : PassClassifier{image_base, {}}
    {
    }

    PassClassifier::PassClassifier(
        const std::uintptr_t image_base,
        const PassClassifierDiagnosticSink diagnostics) noexcept
        : common_2d_vtable_{RelocateOrZero(image_base, kCommon2DVtableRva)},
          common_3d_vtable_{RelocateOrZero(image_base, kCommon3DVtableRva)},
          diagnostics_{diagnostics}
    {
    }

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
        case GameplayPass::orthographic_background:
        case GameplayPass::orthographic_effects:
            return RenderSpace::native_2d;
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
