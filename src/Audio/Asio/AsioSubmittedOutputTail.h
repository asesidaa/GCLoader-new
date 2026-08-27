#pragma once

#include <atomic>
#include <cstdint>

namespace gc::audio
{
    struct AsioSubmittedOutputTailSnapshot final
    {
        std::uint64_t submitted_output_tail{};
        std::uint64_t publication_sequence{};
        bool stable{};
        bool available{};
        bool valid{};
    };

    class AsioSubmittedOutputTail final
    {
    public:
        [[nodiscard]] bool Publish(
            std::uint64_t submitted_output_tail) noexcept;
        [[nodiscard]] AsioSubmittedOutputTailSnapshot Read() const noexcept;
        void Invalidate() noexcept;

    private:
        std::atomic_uint64_t version_{};
        std::atomic_uint64_t submitted_output_tail_{};
        std::atomic_uint64_t publication_sequence_{};
        std::atomic_bool valid_{true};
    };
} // namespace gc::audio
