#pragma once

#include "Audio/Mixer/AudioRenderCore.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>

namespace gc::audio
{
    enum class LogicalRenderOwner : std::uint8_t
    {
        Pump,
        AsioBridge,
    };

    enum class LogicalRenderFailure : std::uint8_t
    {
        Busy,
        InvalidConfiguration,
        LeaseAlreadyActive,
        NoActiveLease,
        InvalidLease,
        SameOwner,
        TailMismatch,
        ArithmeticOverflow,
        GenerationOverflow,
    };

    struct LogicalRenderLease final
    {
        LogicalRenderOwner owner{};
        std::uint64_t generation{};
        std::uint64_t acquired_tail{};
    };

    struct LogicalRenderPlan final
    {
        MixerRenderTimeline timeline{};
        std::uint64_t committed_tail_after{};
        std::uint64_t lease_generation{};
        std::uint64_t claim_token{};
    };

    class LogicalRenderStream final
    {
    public:
        [[nodiscard]] static std::unique_ptr<LogicalRenderStream> Create(
            AudioRenderCore& render_core) noexcept;

        [[nodiscard]]
        std::expected<LogicalRenderLease, LogicalRenderFailure>
        AcquireInitial(LogicalRenderOwner owner) noexcept;
        [[nodiscard]]
        std::expected<LogicalRenderLease, LogicalRenderFailure>
        Transfer(
            const LogicalRenderLease& from,
            LogicalRenderOwner to,
            std::uint64_t expected_tail) noexcept;
        [[nodiscard]]
        std::expected<LogicalRenderPlan, LogicalRenderFailure>
        BeginRender(const LogicalRenderLease& lease) noexcept;
        [[nodiscard]] AudioRenderBlock Render(
            const LogicalRenderPlan& plan) noexcept;
        [[nodiscard]] bool Commit(
            const LogicalRenderPlan& plan) noexcept;
        [[nodiscard]] bool Abandon(
            const LogicalRenderPlan& plan) noexcept;
        [[nodiscard]] std::uint64_t committed_tail() const noexcept;

    private:
        explicit LogicalRenderStream(
            AudioRenderCore& render_core) noexcept;

        [[nodiscard]] bool TryAcquireClaim() noexcept;
        void ReleaseClaim() noexcept;
        void ClearPendingPlan() noexcept;
        [[nodiscard]] bool MatchesActiveLease(
            const LogicalRenderLease& lease) const noexcept;
        [[nodiscard]] bool MatchesPendingPlan(
            const LogicalRenderPlan& plan) const noexcept;
        [[nodiscard]] static AudioRenderBlock
        InvalidRenderBlock() noexcept;

        AudioRenderCore& render_core_;
        const std::uint32_t period_frames_;

        std::atomic_bool claim_active_{};
        std::atomic_uint64_t committed_tail_{};

        LogicalRenderOwner active_owner_{};
        std::uint64_t active_lease_generation_{};
        std::uint64_t next_lease_generation_{};
        bool lease_active_{};

        std::uint64_t next_claim_token_{};
        std::uint64_t active_claim_token_{};
        std::uint64_t pending_begin_{};
        std::uint64_t pending_tail_{};
        std::uint64_t pending_lease_generation_{};
        bool pending_rendered_{};
    };
} // namespace gc::audio
