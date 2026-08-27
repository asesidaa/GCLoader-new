#include "Audio/Asio/AsioSubmittedOutputTail.h"

#include <limits>

namespace gc::audio
{
    namespace
    {
        constexpr int kSnapshotReadAttempts = 3;
    } // namespace

    bool AsioSubmittedOutputTail::Publish(
        const std::uint64_t submitted_output_tail) noexcept
    {
        if (submitted_output_tail == 0 ||
            !valid_.load(std::memory_order_acquire))
        {
            return false;
        }

        std::uint64_t version = version_.load(std::memory_order_acquire);
        if ((version & 1U) != 0 ||
            version > (std::numeric_limits<std::uint64_t>::max)() - 2 ||
            !version_.compare_exchange_strong(version,
                                              version + 1,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
        {
            return false;
        }

        const std::uint64_t current_tail =
            submitted_output_tail_.load(std::memory_order_relaxed);
        const std::uint64_t current_sequence =
            publication_sequence_.load(std::memory_order_relaxed);
        if (!valid_.load(std::memory_order_acquire) ||
            submitted_output_tail <= current_tail ||
            current_sequence == (std::numeric_limits<std::uint64_t>::max)())
        {
            version_.store(version + 2, std::memory_order_release);
            return false;
        }

        submitted_output_tail_.store(submitted_output_tail,
                                     std::memory_order_relaxed);
        publication_sequence_.store(current_sequence + 1,
                                    std::memory_order_relaxed);
        version_.store(version + 2, std::memory_order_release);
        return valid_.load(std::memory_order_acquire);
    }

    AsioSubmittedOutputTailSnapshot AsioSubmittedOutputTail::Read() const noexcept
    {
        for (int attempt = 0; attempt < kSnapshotReadAttempts; ++attempt)
        {
            if (!valid_.load(std::memory_order_acquire))
            {
                return {
                    .stable = true,
                    .valid = false,
                };
            }

            const std::uint64_t before = version_.load(std::memory_order_acquire);
            if ((before & 1U) != 0)
            {
                continue;
            }

            const std::uint64_t tail =
                submitted_output_tail_.load(std::memory_order_relaxed);
            const std::uint64_t sequence =
                publication_sequence_.load(std::memory_order_relaxed);
            const std::uint64_t after = version_.load(std::memory_order_acquire);
            const bool valid = valid_.load(std::memory_order_acquire);
            if (before == after && (after & 1U) == 0)
            {
                return {
                    .submitted_output_tail = tail,
                    .publication_sequence = sequence,
                    .stable = true,
                    .available = sequence != 0,
                    .valid = valid,
                };
            }
        }

        return {
            .stable = false,
            .valid = valid_.load(std::memory_order_acquire),
        };
    }

    void AsioSubmittedOutputTail::Invalidate() noexcept
    {
        valid_.store(false, std::memory_order_release);
    }
} // namespace gc::audio
