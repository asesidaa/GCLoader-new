#include "Config/ConfigCompiler.h"

#include "Config/DeclaredEnum.h"
#include "Config/Validation/ExperimentalValidation.h"
#include "Config/Validation/InputValidation.h"
#include "Config/Validation/RegistryValidation.h"
#include "Config/RegistryConfig.h"
#include "Nesys/Network/NesysNetworkConfig.h"

#include <array>
#include <climits>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


namespace gc::config
{
    ValidatedConfig::ValidatedConfig(
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
        const bool enable_auto_play,
        bool unlock_all_songs_and_difficulties)
        : logging_(std::move(logging)),
          input_(std::move(input)),
          switch_input_(std::move(switch_input)),
          audio_(std::move(audio)),
          framerate_(std::move(framerate)),
          judgement_(std::move(judgement)),
          nesys_(std::move(nesys)),
          rfid_(std::move(rfid)),
          system_path_(std::move(system_path)),
          windowed_widescreen_(std::move(windowed_widescreen)),
          enable_auto_play_(enable_auto_play),
          unlock_all_songs_and_difficulties_(
              unlock_all_songs_and_difficulties)
    {
    }

    std::expected<ValidatedConfig, ConfigErrors>
    ConfigCompiler::Compile(const ConfigDocument& document) noexcept
    {
        try
        {
            ConfigErrors errors;
            validation::ValidationContext context{errors};
            const bool poll_valid = validation::ValidateInput(document, context);

            auto registry = validation::ValidateRegistry(document, context);
            auto& server_octets = registry.server_octets;
            auto& derived_paths = registry.derived_paths;

            const auto log_level = document.logging().level();
            if (!IsDeclaredEnumValue(log_level))
            {
                errors.push_back({
                    .path = ConfigPath{"logging", "level"},
                    .code = ConfigErrorCode::unsupported_value,
                    .message = "unsupported loader log level",
                });
            }

            const auto [target_fps, widescreen_width, widescreen_height,
                        widescreen_hud_placement, audio_backend] =
                validation::ValidateExperimental(document, poll_valid, context);

            if (!errors.empty())
            {
                return std::unexpected(std::move(errors));
            }

            std::vector<input::ControllerBinding> bindings;
            bindings.reserve(document.controller().bindings().size());
            for (const auto& binding : document.controller().bindings())
            {
                const input::HidControlAddress address{
                    .usage_page = static_cast<std::uint16_t>(
                        binding.usage_page.value_or(0)),
                    .usage = static_cast<std::uint16_t>(
                        binding.usage.value_or(0)),
                    .link_collection = static_cast<std::uint16_t>(
                        binding.link_collection.value_or(0)),
                    .report_id = static_cast<std::uint8_t>(
                        binding.report_id.value_or(0)),
                };
                switch (binding.type)
                {
                case input::DigitalControlType::XInputButton:
                    bindings.emplace_back(input::XInputButtonBinding{
                        binding.action,
                        *binding.control,
                    });
                    break;
                case input::DigitalControlType::XInputAxis:
                    bindings.emplace_back(input::XInputAxisBinding{
                        binding.action,
                        *binding.control,
                        *binding.direction,
                    });
                    break;
                case input::DigitalControlType::XInputTrigger:
                    bindings.emplace_back(input::XInputTriggerBinding{
                        binding.action,
                        *binding.control,
                    });
                    break;
                case input::DigitalControlType::RawHidButton:
                    bindings.emplace_back(input::RawHidButtonBinding{
                        binding.action,
                        address,
                    });
                    break;
                case input::DigitalControlType::RawHidValue:
                    bindings.emplace_back(input::RawHidValueBinding{
                        binding.action,
                        address,
                        *binding.direction,
                        *binding.neutral_value,
                    });
                    break;
                case input::DigitalControlType::RawHidHat:
                    bindings.emplace_back(input::RawHidHatBinding{
                        binding.action,
                        address,
                        *binding.direction,
                    });
                    break;
                }
            }

            const auto backend = document.controller().backend();
            input::ControllerSettings controller =
                backend == input::ControllerBackend::XInput
                    ? input::ControllerSettings{
                        input::XInputControllerSettings{
                            static_cast<std::uint32_t>(
                                document.controller().device_id().front() - '0'),
                            std::move(bindings),
                        },
                    }
                    : input::ControllerSettings{
                        input::RawHidControllerSettings{
                            document.controller().device_id(),
                            std::move(bindings),
                        },
                    };

            const bool absolute =
                document.experimental()
                        .enable_absolute_time_judgement();
            audio::AudioBackendSettings audio_selection =
                audio::DirectSoundSettings{};
            std::optional<audio::ExactJudgementTimelineDomain> clock_domain;
            if (audio_backend ==
                audio::AudioBackend::wasapi_exclusive)
            {
                audio_selection = audio::WasapiExclusiveSettings{
                    static_cast<std::uint32_t>(
                        document.experimental()
                                .wasapi_exclusive_buffer_ms()),
                    absolute,
                };
                clock_domain = audio::ExactJudgementTimelineDomain::WasapiQpc;
            }
            else if (audio_backend == audio::AudioBackend::asio)
            {
                audio_selection = audio::AsioSettings{
                    document.experimental().asio_driver_name(),
                    static_cast<std::uint32_t>(
                        document.experimental().asio_buffer_frames()),
                    static_cast<std::uint32_t>(
                        document.experimental()
                                .asio_output_base_channel()),
                };
            }

            std::optional<nesys_service::RegistryOverrideValues>
                registry_override;
            if (document.registry().enabled())
            {
                registry_override =
                    nesys_service::RegistryOverrideValues{
                        static_cast<std::uint32_t>(
                            document.registry().game().country()),
                        static_cast<std::uint32_t>(
                            document.registry().nesys().game_kind()),
                        static_cast<std::uint32_t>(
                            document.registry().nesys().event_next_time()),
                        static_cast<std::uint32_t>(
                            document.registry().nesys().condition_time()),
                        0,
                        static_cast<std::uint32_t>(
                            document.registry().nesys().log_level()),
                        std::move(derived_paths->news),
                        std::move(derived_paths->event),
                        std::move(derived_paths->log),
                    };
            }
            auto server_ansi = nesys_service::FormatDottedDecimalIpv4(
                *server_octets);
            std::wstring server_wide;
            server_wide.reserve(server_ansi.size());
            for (const char character : server_ansi)
            {
                server_wide.push_back(
                    static_cast<unsigned char>(character));
            }

            return ValidatedConfig{
                logging::LoggingSettings{log_level},
                input::InputSettings{
                    document.input_poll_hz(),
                    absolute,
                    document.input_mode(),
                    document.axis_press_threshold_percent(),
                    document.axis_release_threshold_percent(),
                    validation::CompileKeyboard(document.keyboard()),
                    std::move(controller),
                },
                switch_input::SwitchInputSettings{
                    document.gameplay_input_style(),
                },
                audio::AudioSettings{
                    audio_backend,
                    std::move(audio_selection),
                },
                framerate::FramerateSettings{
                    target_fps,
                    document.experimental().enable_timer_freeze_patches(),
                },
                absolute_judgement::JudgementSettings{
                    absolute,
                    target_fps,
                    document.input_poll_hz(),
                    audio_backend,
                    clock_domain,
                },
                nesys_service::NesysSettings{
                    document.experimental()
                            .enable_nesys_service_adapter_patch(),
                    nesys_service::ServerAddressState{
                        *server_octets,
                        std::move(server_ansi),
                        std::move(server_wide),
                    },
                    std::move(registry_override),
                },
                rfid::FeatureSettings{
                    document.keyboard().card_read(),
                    document.experimental()
                            .enable_testmode_storage_redirect(),
                },
                system_path::SystemPathSettings{
                    document.registry().enabled(),
                    document.registry().system_path(),
                },
                windowed_widescreen::WindowedWidescreenSettings{
                    document.experimental()
                            .enable_windowed_widescreen_stage(),
                    static_cast<std::uint32_t>(widescreen_width),
                    static_cast<std::uint32_t>(widescreen_height),
                    widescreen_hud_placement,
                },
                document.experimental().enable_auto_play(),
                document.experimental()
                        .unlock_all_songs_and_difficulties(),
            };
        }
        catch (const std::exception& error)
        {
            return std::unexpected(ConfigErrors{
                {
                    .path = ConfigPath{"configuration"},
                    .code = ConfigErrorCode::invalid_value,
                    .message =
                    "configuration compilation failed: " +
                    std::string{error.what()},
                }
            });
        }
        catch (...)
        {
            return std::unexpected(ConfigErrors{
                {
                    .path = ConfigPath{"configuration"},
                    .code = ConfigErrorCode::invalid_value,
                    .message = "configuration compilation failed unexpectedly",
                }
            });
        }
    }
} // namespace gc::config
