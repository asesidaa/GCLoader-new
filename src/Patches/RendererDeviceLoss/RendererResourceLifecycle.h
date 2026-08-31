#pragma once

#include <cstdint>
#include <expected>

namespace gc::renderer_device_loss
{
    enum class RendererResourceState : std::uint8_t
    {
        disabled,
        awaiting_device,
        active,
        releasing_for_reset,
        awaiting_reset,
        recreating,
    };

    enum class RendererResourceError : std::uint8_t
    {
        invalid_participant,
        invalid_renderer_owner,
        invalid_order,
        create_failed,
        runtime_unavailable,
    };

    struct RendererResourceParticipant final
    {
        void* context{};
        bool (*create)(void*, std::uintptr_t renderer_owner) noexcept{};
        void (*release)(void*) noexcept{};
    };

    class RendererResourceLifecycle final
    {
    public:
        [[nodiscard]] std::expected<void, RendererResourceError> Attach(
            RendererResourceParticipant participant) noexcept;

        [[nodiscard]] std::expected<void, RendererResourceError>
        OnDeviceCreated(std::uintptr_t renderer_owner) noexcept;

        [[nodiscard]] std::expected<void, RendererResourceError>
        BeforeReset() noexcept;

        [[nodiscard]] std::expected<void, RendererResourceError> AfterReset(
            std::uintptr_t renderer_owner) noexcept;

        void Detach() noexcept;

        [[nodiscard]] RendererResourceState state() const noexcept
        {
            return state_;
        }

    private:
        RendererResourceParticipant participant_{};
        RendererResourceState state_{RendererResourceState::disabled};
    };
} // namespace gc::renderer_device_loss
