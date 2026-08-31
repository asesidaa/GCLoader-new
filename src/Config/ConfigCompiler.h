#pragma once

#include "Audio/AudioSettings.h"
#include "Config/ConfigDocument.h"
#include "Config/ConfigError.h"
#include "Input/Switch/SwitchInputSettings.h"
#include "Input/Types/InputSettings.h"
#include "Logging/LoggingSettings.h"
#include "Nesys/NesysSettings.h"
#include "Patches/AbsoluteJudgement/JudgementSettings.h"
#include "Patches/Framerate/FramerateSettings.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenSettings.h"
#include "Rfid/FeatureSettings.h"
#include "SystemPath/SystemPathSettings.h"

#include <expected>

namespace gc::config
{
    class ValidatedConfig final
    {
    public:
        [[nodiscard]] const logging::LoggingSettings& logging() const noexcept
        {
            return logging_;
        }

        [[nodiscard]] const input::InputSettings& input() const noexcept
        {
            return input_;
        }

        [[nodiscard]] const switch_input::SwitchInputSettings&
        switch_input() const noexcept
        {
            return switch_input_;
        }

        [[nodiscard]] const audio::AudioSettings& audio() const noexcept
        {
            return audio_;
        }

        [[nodiscard]] const framerate::FramerateSettings&
        framerate() const noexcept
        {
            return framerate_;
        }

        [[nodiscard]] const absolute_judgement::JudgementSettings&
        judgement() const noexcept
        {
            return judgement_;
        }

        [[nodiscard]] const nesys_service::NesysSettings& nesys() const noexcept
        {
            return nesys_;
        }

        [[nodiscard]] const rfid::FeatureSettings& rfid() const noexcept
        {
            return rfid_;
        }

        [[nodiscard]] const system_path::SystemPathSettings&
        system_path() const noexcept
        {
            return system_path_;
        }

        [[nodiscard]] bool unlock_all_songs_and_difficulties() const noexcept
        {
            return unlock_all_songs_and_difficulties_;
        }

        [[nodiscard]] const windowed_widescreen::WindowedWidescreenSettings&
        windowed_widescreen() const noexcept
        {
            return windowed_widescreen_;
        }

    private:
        ValidatedConfig(
            logging::LoggingSettings logging,
            input::InputSettings input,
            switch_input::SwitchInputSettings switch_input,
            audio::AudioSettings audio,
            framerate::FramerateSettings framerate,
            absolute_judgement::JudgementSettings judgement,
            nesys_service::NesysSettings nesys,
            rfid::FeatureSettings rfid,
            system_path::SystemPathSettings system_path,
            windowed_widescreen::WindowedWidescreenSettings
                windowed_widescreen,
            bool unlock_all_songs_and_difficulties);

        friend class ConfigCompiler;
        logging::LoggingSettings logging_;
        input::InputSettings input_;
        switch_input::SwitchInputSettings switch_input_;
        audio::AudioSettings audio_;
        framerate::FramerateSettings framerate_;
        absolute_judgement::JudgementSettings judgement_;
        nesys_service::NesysSettings nesys_;
        rfid::FeatureSettings rfid_;
        system_path::SystemPathSettings system_path_;
        windowed_widescreen::WindowedWidescreenSettings
            windowed_widescreen_;
        bool unlock_all_songs_and_difficulties_{};
    };

    class ConfigCompiler final
    {
    public:
        [[nodiscard]] static std::expected<ValidatedConfig, ConfigErrors>
        Compile(const ConfigDocument& document) noexcept;
    };
} // namespace gc::config
