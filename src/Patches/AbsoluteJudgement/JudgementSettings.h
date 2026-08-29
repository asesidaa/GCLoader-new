#pragma once

#include "Audio/ExactJudgementTimeline.h"
#include "Audio/AudioSettings.h"

#include <cstdint>
#include <optional>

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::absolute_judgement
{
    class JudgementSettings final
    {
    public:
        [[nodiscard]] bool enabled() const noexcept
        {
            return enabled_;
        }

        [[nodiscard]] std::uint32_t target_fps() const noexcept
        {
            return target_fps_;
        }

        [[nodiscard]] std::uint32_t input_rate_hz() const noexcept
        {
            return input_rate_hz_;
        }

        [[nodiscard]] audio::AudioBackend audio_backend() const noexcept
        {
            return audio_backend_;
        }

        [[nodiscard]] std::optional<audio::ExactJudgementTimelineDomain>
        expected_clock_domain() const noexcept
        {
            return expected_clock_domain_;
        }

    private:
        JudgementSettings(
            bool enabled,
            std::uint32_t target_fps,
            std::uint32_t input_rate_hz,
            audio::AudioBackend audio_backend,
            std::optional<audio::ExactJudgementTimelineDomain>
            expected_clock_domain) noexcept
            : enabled_(enabled),
              target_fps_(target_fps),
              input_rate_hz_(input_rate_hz),
              audio_backend_(audio_backend),
              expected_clock_domain_(expected_clock_domain)
        {
        }

        friend class gc::config::ConfigCompiler;
        bool enabled_{};
        std::uint32_t target_fps_{};
        std::uint32_t input_rate_hz_{};
        audio::AudioBackend audio_backend_{};
        std::optional<audio::ExactJudgementTimelineDomain> expected_clock_domain_;
    };
} // namespace gc::absolute_judgement
