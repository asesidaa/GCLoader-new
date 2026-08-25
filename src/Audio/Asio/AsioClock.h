#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Mixer/PresentedOutputClock.h"

#include <atomic>
#include <cstdint>
#include <optional>

namespace gc::audio {

enum class AsioClockDecisionKind : std::uint8_t {
    priming,
    stable,
    invalid,
};

struct AsioClockDecision {
    AsioClockDecisionKind kind{AsioClockDecisionKind::invalid};
    std::uint64_t presented_output_frame{};
    std::uint64_t render_output_frame_begin{};
    std::uint64_t system_time_ns{};
};

class AsioClockTracker final {
public:
    void Reset(
        std::uint32_t buffer_frames,
        std::uint32_t output_latency_frames) noexcept;
    [[nodiscard]] AsioClockDecision Observe(
        std::uint64_t sample_position,
        std::uint64_t system_time_ns) noexcept;

private:
    [[nodiscard]] AsioClockDecision Fault() noexcept;

    std::uint32_t buffer_frames_{};
    std::uint32_t output_latency_frames_{};
    std::uint64_t previous_sample_position_{};
    std::uint32_t previous_system_time_ms_{};
    std::uint8_t observation_count_{};
    bool configured_{};
    bool faulted_{};
};

struct AsioClockNowActions {
    void* context{};
    std::uint32_t (*time_get_time_ms)(void*) noexcept{};
};

class AsioPresentedClockPublication final
    : public IPresentedOutputClock {
public:
    explicit AsioPresentedClockPublication(
        AsioClockNowActions actions) noexcept;

    void Publish(
        const AsioClockDecision& decision,
        std::uint64_t submitted_output_tail) noexcept;
    void PublishContinuityAnchor(
        std::uint64_t presented_output_frame,
        std::uint64_t submitted_output_tail,
        std::uint64_t system_time_ns) noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
    void Invalidate() noexcept override;

private:
    struct Anchor {
        std::uint64_t presented_output_frame{};
        std::uint64_t submitted_output_tail{};
        std::uint32_t system_time_ms{};
        bool valid{};
    };

    [[nodiscard]] std::optional<Anchor> ReadStable() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
        LastReturned() const noexcept;
    std::uint64_t RememberMonotonic(std::uint64_t frame) noexcept;
    void StoreAnchor(const Anchor& anchor) noexcept;

    AsioClockNowActions actions_{};
    std::atomic_uint64_t sequence_{};
    mutable Anchor anchor_{};
    std::uint64_t writer_generation_{};
    std::atomic_uint64_t last_returned_{};
    std::atomic_bool has_last_returned_{};
};

} // namespace gc::audio
