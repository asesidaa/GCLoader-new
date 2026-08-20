#pragma once

#include "Audio/ExactAudioTime.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace gc::audio {

struct ExactWasapiAnchor {
    std::uint64_t sequence{};
    std::uint64_t endpoint_generation{};
    std::uint64_t endpoint_position{};
    std::uint64_t qpc_100ns{};
    EndpointClockMapping mapping{};
    std::uint64_t submitted_output_tail{};
};

class ExactWasapiClock final {
public:
    ~ExactWasapiClock();

    static std::shared_ptr<ExactWasapiClock> Create(
        std::uint64_t endpoint_generation,
        std::uint32_t output_sample_rate,
        std::uint64_t clock_frequency,
        std::int64_t qpc_frequency,
        std::uint32_t period_frames) noexcept;
    void Publish(const ExactWasapiAnchor&) noexcept;
    void Invalidate() noexcept;
    ExactOutputClockResult ResolveQpc(
        std::int64_t raw_qpc_ticks) const noexcept;
    [[nodiscard]] std::uint64_t endpoint_generation() const noexcept;
    [[nodiscard]] std::int64_t qpc_frequency() const noexcept;
    [[nodiscard]] std::uint64_t publication_count() const noexcept;

private:
    struct Slot;

    ExactWasapiClock(
        std::uint64_t endpoint_generation,
        std::uint32_t output_sample_rate,
        std::uint64_t clock_frequency,
        std::int64_t qpc_frequency,
        std::size_t capacity,
        std::unique_ptr<Slot[]> slots) noexcept;

    [[nodiscard]] bool ReadStable(
        std::uint64_t publication,
        ExactWasapiAnchor* anchor) const noexcept;

    std::uint64_t endpoint_generation_{};
    std::uint32_t output_sample_rate_{};
    std::uint64_t clock_frequency_{};
    std::int64_t qpc_frequency_{};
    std::size_t capacity_{};
    std::unique_ptr<Slot[]> slots_;
    std::atomic<std::uint64_t> published_count_{};
    std::atomic<bool> invalidated_{};
    std::uint64_t writer_publication_count_{};
    bool writer_has_anchor_{};
    ExactWasapiAnchor writer_previous_{};
};

std::shared_ptr<const ExactWasapiClock>
AcquireExactWasapiClock() noexcept;

namespace detail {

[[nodiscard]] std::uint64_t
NextExactWasapiClockGeneration() noexcept;
[[nodiscard]] bool RegisterExactWasapiClock(
    const std::shared_ptr<ExactWasapiClock>& provider) noexcept;
void UnregisterExactWasapiClock(
    std::uint64_t expected_generation) noexcept;

} // namespace detail

} // namespace gc::audio
