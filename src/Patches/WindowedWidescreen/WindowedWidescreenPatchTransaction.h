#pragma once

#include "Patches/WindowedWidescreen/WindowedWidescreenAbi.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::windowed_widescreen
{
    inline constexpr std::size_t kMaximumWidescreenHooks = 36;

    struct WidescreenContractManifest
    {
        std::span<const WidescreenByteContract> byte_contracts;
        std::span<const WidescreenPointerContract> pointer_contracts;
    };

    struct WidescreenHookRequest
    {
        WidescreenContractSite site{WidescreenContractSite::none};
        void* callback{};
    };

    struct WidescreenInstallActions
    {
        void* context{};
        bool (*read)(
            void*, std::uintptr_t, std::span<std::byte>) noexcept{};
        bool (*prepare_candidate)(void*) noexcept{};
        bool (*create_disabled)(
            void*,
            WidescreenContractSite,
            WidescreenHookKind,
            std::uintptr_t,
            void*) noexcept{};
        bool (*enable)(void*, WidescreenContractSite) noexcept{};
        void (*reset)(void*, WidescreenContractSite) noexcept{};
        void (*detach_renderer_resource)(void*) noexcept{};
        void (*clear_callback_context)(void*) noexcept{};
        bool (*publish_owner)(void*) noexcept{};
    };

    enum class WidescreenInstallStage : std::uint8_t
    {
        none,
        invalid_actions,
        unexpected_image_base,
        invalid_manifest,
        invalid_request,
        capacity_overflow,
        address_overflow,
        preflight_read,
        byte_mismatch,
        pointer_mismatch,
        candidate_prepare,
        hook_create,
        hook_enable,
        owner_publish,
    };

    struct WidescreenInstallError
    {
        WidescreenInstallStage stage{WidescreenInstallStage::none};
        WidescreenContractSite site{WidescreenContractSite::none};
        std::size_t index{};
        bool rollback_attempted{};
        bool rollback_complete{};
    };

    [[nodiscard]] std::expected<void, WidescreenInstallError>
    InstallWindowedWidescreenHooks(
        std::uintptr_t image_base,
        WidescreenContractManifest manifest,
        std::span<const WidescreenHookRequest> requests,
        const WidescreenInstallActions& actions) noexcept;
} // namespace gc::windowed_widescreen
