#pragma once

#include "Patches/WindowedWidescreen/RenderSpacePolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gc::windowed_widescreen
{
    enum class GameplayPass : std::uint8_t
    {
        stage_background,
        perspective_track,
        orthographic_effects,
    };

    struct PassClassifierDiagnosticSink
    {
        void* context{};
        void (*unknown_identity)(void*, std::uintptr_t) noexcept{};
        void (*unknown_capacity_exhausted)(void*) noexcept{};
    };

    class PassClassifier final
    {
    public:
        static constexpr std::size_t kUnknownIdentityCapacity = 32;

        explicit PassClassifier(std::uintptr_t image_base) noexcept;
        PassClassifier(
            std::uintptr_t image_base,
            PassClassifierDiagnosticSink diagnostics) noexcept;

        [[nodiscard]] RenderSpace ClassifyTask(
            std::uintptr_t task_vtable) noexcept;

        [[nodiscard]] static RenderSpace ClassifyGameplay(
            GameplayPass pass) noexcept;

        [[nodiscard]] static constexpr std::size_t
        unknown_identity_capacity() noexcept
        {
            return kUnknownIdentityCapacity;
        }

        [[nodiscard]] std::size_t unknown_identity_count() const noexcept
        {
            return unknown_identity_count_;
        }

        [[nodiscard]] bool unknown_capacity_exhausted() const noexcept
        {
            return unknown_capacity_exhausted_;
        }

    private:
        void RecordUnknown(std::uintptr_t task_vtable) noexcept;

        std::uintptr_t common_2d_vtable_{};
        std::uintptr_t common_3d_vtable_{};
        PassClassifierDiagnosticSink diagnostics_{};
        std::array<std::uintptr_t, kUnknownIdentityCapacity>
            unknown_identities_{};
        std::size_t unknown_identity_count_{};
        bool unknown_capacity_exhausted_{};
    };
} // namespace gc::windowed_widescreen
