#include "Patches/RendererDeviceLoss/RendererResourceLifecycle.h"

namespace gc::renderer_device_loss
{
    std::expected<void, RendererResourceError>
    RendererResourceLifecycle::Attach(
        const RendererResourceParticipant participant) noexcept
    {
        if (participant.context == nullptr || participant.create == nullptr ||
            participant.release == nullptr)
        {
            return std::unexpected(
                RendererResourceError::invalid_participant);
        }
        if (state_ != RendererResourceState::disabled)
        {
            return std::unexpected(RendererResourceError::invalid_order);
        }

        participant_ = participant;
        state_ = RendererResourceState::awaiting_device;
        return {};
    }

    std::expected<void, RendererResourceError>
    RendererResourceLifecycle::OnDeviceCreated(
        const std::uintptr_t renderer_owner) noexcept
    {
        if (renderer_owner == 0)
        {
            return std::unexpected(
                RendererResourceError::invalid_renderer_owner);
        }
        if (state_ != RendererResourceState::awaiting_device)
        {
            return std::unexpected(RendererResourceError::invalid_order);
        }

        state_ = RendererResourceState::recreating;
        if (!participant_.create(participant_.context, renderer_owner))
        {
            state_ = RendererResourceState::awaiting_device;
            return std::unexpected(RendererResourceError::create_failed);
        }
        state_ = RendererResourceState::active;
        return {};
    }

    std::expected<void, RendererResourceError>
    RendererResourceLifecycle::BeforeReset() noexcept
    {
        if (state_ == RendererResourceState::awaiting_reset)
        {
            return {};
        }
        if (state_ != RendererResourceState::active)
        {
            return std::unexpected(RendererResourceError::invalid_order);
        }

        state_ = RendererResourceState::releasing_for_reset;
        participant_.release(participant_.context);
        state_ = RendererResourceState::awaiting_reset;
        return {};
    }

    std::expected<void, RendererResourceError>
    RendererResourceLifecycle::AfterReset(
        const std::uintptr_t renderer_owner) noexcept
    {
        if (renderer_owner == 0)
        {
            return std::unexpected(
                RendererResourceError::invalid_renderer_owner);
        }
        if (state_ != RendererResourceState::awaiting_reset)
        {
            return std::unexpected(RendererResourceError::invalid_order);
        }

        state_ = RendererResourceState::recreating;
        if (!participant_.create(participant_.context, renderer_owner))
        {
            state_ = RendererResourceState::awaiting_reset;
            return std::unexpected(RendererResourceError::create_failed);
        }
        state_ = RendererResourceState::active;
        return {};
    }

    void RendererResourceLifecycle::Detach() noexcept
    {
        if (state_ == RendererResourceState::disabled)
        {
            return;
        }
        if (state_ == RendererResourceState::active)
        {
            participant_.release(participant_.context);
        }

        participant_ = {};
        state_ = RendererResourceState::disabled;
    }
} // namespace gc::renderer_device_loss
