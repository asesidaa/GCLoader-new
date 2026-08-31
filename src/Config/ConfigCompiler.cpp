#include "Config/ConfigCompiler.h"

#include "Config/RegistryConfig.h"
#include "Nesys/Network/NesysNetworkConfig.h"
#include "Patches/Framerate/FrameratePolicy.h"

#include <array>
#include <climits>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rfl.hpp>

namespace gc::config
{
    namespace
    {
        using TargetFpsValidator = rfl::Validator<
            std::uint32_t,
            rfl::Minimum<framerate::kMinimumTargetFps>,
            rfl::Maximum<framerate::kMaximumTargetFps>>;
        using InputPollValidator = rfl::Validator<
            std::uint32_t,
            rfl::OneOf <
            rfl::EqualTo < 125>
        ,
        rfl::EqualTo<250>
        ,
        rfl::EqualTo<500>
        ,
        rfl::EqualTo<1000>
        >
        >;
        using PercentValidator =
        rfl::Validator<std::uint32_t, rfl::Maximum<100>>;
        using NonZeroWasapiBufferValidator =
        rfl::Validator<std::uint32_t, rfl::Minimum<1>>;
        using RegistryDwordValidator = rfl::Validator<
            std::int64_t,
            rfl::Minimum < 0>
        ,
        rfl::Maximum<4294967295LL>
        >;
        using RegistryLogLevelValidator = rfl::Validator<
            std::int64_t,
            rfl::Minimum < 0>
        ,
        rfl::Maximum<3>
        >;

        bool IsUtf8ContinuationByte(unsigned char value) noexcept
        {
            return value >= 0x80U && value <= 0xBFU;
        }

        bool IsValidUtf8(std::string_view value) noexcept
        {
            std::size_t index = 0;
            while (index < value.size())
            {
                const auto first =
                    static_cast<unsigned char>(value[index]);
                if (first <= 0x7FU)
                {
                    ++index;
                    continue;
                }
                if (first >= 0xC2U && first <= 0xDFU)
                {
                    if (index + 1 >= value.size() ||
                        !IsUtf8ContinuationByte(
                            static_cast<unsigned char>(value[index + 1])))
                    {
                        return false;
                    }
                    index += 2;
                    continue;
                }
                if (first >= 0xE0U && first <= 0xEFU)
                {
                    if (index + 2 >= value.size())
                    {
                        return false;
                    }
                    const auto second =
                        static_cast<unsigned char>(value[index + 1]);
                    const auto third =
                        static_cast<unsigned char>(value[index + 2]);
                    const bool valid_second =
                        first == 0xE0U
                            ? second >= 0xA0U && second <= 0xBFU
                            : first == 0xEDU
                            ? second >= 0x80U && second <= 0x9FU
                            : IsUtf8ContinuationByte(second);
                    if (!valid_second || !IsUtf8ContinuationByte(third))
                    {
                        return false;
                    }
                    index += 3;
                    continue;
                }
                if (first >= 0xF0U && first <= 0xF4U)
                {
                    if (index + 3 >= value.size())
                    {
                        return false;
                    }
                    const auto second =
                        static_cast<unsigned char>(value[index + 1]);
                    const auto third =
                        static_cast<unsigned char>(value[index + 2]);
                    const auto fourth =
                        static_cast<unsigned char>(value[index + 3]);
                    const bool valid_second =
                        first == 0xF0U
                            ? second >= 0x90U && second <= 0xBFU
                            : first == 0xF4U
                            ? second >= 0x80U && second <= 0x8FU
                            : IsUtf8ContinuationByte(second);
                    if (!valid_second ||
                        !IsUtf8ContinuationByte(third) ||
                        !IsUtf8ContinuationByte(fourth))
                    {
                        return false;
                    }
                    index += 4;
                    continue;
                }
                return false;
            }
            return true;
        }

        struct ValidPhysicalKeyRule
        {
            [[maybe_unused]] static rfl::Result<input::PhysicalKey> validate(
                input::PhysicalKey key) noexcept
            {
                if (key.make_code != 0)
                {
                    return key;
                }
                return rfl::error("physical scan-code token must be nonzero");
            }
        };

        struct ValidAsioDriverTextRule
        {
            [[maybe_unused]] static rfl::Result<std::string> validate(
                const std::string& value) noexcept
            {
                if (value.size() <= 1024 && IsValidUtf8(value))
                {
                    return value;
                }
                return rfl::error(
                    "ASIO driver name must be at most 1024 valid UTF-8 bytes");
            }
        };

        using PhysicalKeyValidator =
        rfl::Validator<input::PhysicalKey, ValidPhysicalKeyRule>;
        using AsioDriverTextValidator =
        rfl::Validator<std::string, ValidAsioDriverTextRule>;

        template <class Validator, class T>
        bool ValidateLeaf(
            const T& value,
            ConfigPath path,
            ConfigErrorCode code,
            std::string message,
            ConfigErrors& errors)
        {
            if (Validator::from_value(value))
            {
                return true;
            }
            errors.push_back({
                .path = std::move(path),
                .code = code,
                .message = std::move(message),
            });
            return false;
        }

        bool IsXInputButton(input::XInputControl control) noexcept
        {
            return control >= input::XInputControl::A &&
                control <= input::XInputControl::RightThumb;
        }

        bool IsXInputAxis(input::XInputControl control) noexcept
        {
            return control >= input::XInputControl::LeftX &&
                control <= input::XInputControl::RightY;
        }

        bool IsXInputTrigger(input::XInputControl control) noexcept
        {
            return control == input::XInputControl::LeftTrigger ||
                control == input::XInputControl::RightTrigger;
        }

        bool IsAxisDirection(input::ControlDirection direction) noexcept
        {
            return direction == input::ControlDirection::Positive ||
                direction == input::ControlDirection::Negative;
        }

        bool IsCardinalDirection(input::ControlDirection direction) noexcept
        {
            return direction == input::ControlDirection::Up ||
                direction == input::ControlDirection::Down ||
                direction == input::ControlDirection::Left ||
                direction == input::ControlDirection::Right;
        }

        bool IsXInputType(input::DigitalControlType type) noexcept
        {
            return type == input::DigitalControlType::XInputButton ||
                type == input::DigitalControlType::XInputAxis ||
                type == input::DigitalControlType::XInputTrigger;
        }

        bool HasCompleteHidAddress(
            const input::DigitalControlBinding& binding) noexcept
        {
            if (!binding.usage_page || !binding.usage ||
                !binding.link_collection || !binding.report_id)
            {
                return false;
            }
            return *binding.usage_page != 0 && *binding.usage != 0 &&
                *binding.usage_page <=
                std::numeric_limits<std::uint16_t>::max() &&
                *binding.usage <= std::numeric_limits<std::uint16_t>::max() &&
                *binding.link_collection <=
                std::numeric_limits<std::uint16_t>::max() &&
                *binding.report_id <= std::numeric_limits<std::uint8_t>::max();
        }

        bool BindingFieldsValid(
            const input::DigitalControlBinding& binding) noexcept
        {
            const bool any_hid =
                binding.usage_page || binding.usage ||
                binding.link_collection || binding.report_id;
            switch (binding.type)
            {
            case input::DigitalControlType::XInputButton:
                return binding.control &&
                    IsXInputButton(*binding.control) &&
                    !binding.direction && !any_hid && !binding.neutral_value;
            case input::DigitalControlType::XInputAxis:
                return binding.control &&
                    IsXInputAxis(*binding.control) &&
                    binding.direction &&
                    IsAxisDirection(*binding.direction) &&
                    !any_hid && !binding.neutral_value;
            case input::DigitalControlType::XInputTrigger:
                return binding.control &&
                    IsXInputTrigger(*binding.control) &&
                    !binding.direction && !any_hid && !binding.neutral_value;
            case input::DigitalControlType::RawHidButton:
                return !binding.control && !binding.direction &&
                    !binding.neutral_value && HasCompleteHidAddress(binding);
            case input::DigitalControlType::RawHidValue:
                return !binding.control && binding.direction &&
                    IsAxisDirection(*binding.direction) &&
                    binding.neutral_value && HasCompleteHidAddress(binding);
            case input::DigitalControlType::RawHidHat:
                return !binding.control && !binding.neutral_value &&
                    binding.direction &&
                    IsCardinalDirection(*binding.direction) &&
                    HasCompleteHidAddress(binding);
            }
            return false;
        }

        std::array<std::pair<std::string_view, input::PhysicalKey>, 18>
        KeyboardFields(const NativeKeyboardConfig& keyboard)
        {
            return {
                {
                    {"left_booster_up", keyboard.left_booster_up()},
                    {"left_booster_down", keyboard.left_booster_down()},
                    {"left_booster_left", keyboard.left_booster_left()},
                    {"left_booster_right", keyboard.left_booster_right()},
                    {"left_booster_button", keyboard.left_booster_button()},
                    {"right_booster_up", keyboard.right_booster_up()},
                    {"right_booster_down", keyboard.right_booster_down()},
                    {"right_booster_left", keyboard.right_booster_left()},
                    {"right_booster_right", keyboard.right_booster_right()},
                    {"right_booster_button", keyboard.right_booster_button()},
                    {"test", keyboard.test()},
                    {"service1", keyboard.service1()},
                    {"service2", keyboard.service2()},
                    {"service3", keyboard.service3()},
                    {"p1_start", keyboard.p1_start()},
                    {"p2_start", keyboard.p2_start()},
                    {"p2_service", keyboard.p2_service()},
                    {"card_read", keyboard.card_read()},
                }
            };
        }

        std::vector<input::KeyboardBinding> CompileKeyboard(
            const NativeKeyboardConfig& keyboard)
        {
            using enum input::LogicalAction;
            return {
                {LeftBoosterUp, keyboard.left_booster_up()},
                {LeftBoosterDown, keyboard.left_booster_down()},
                {LeftBoosterLeft, keyboard.left_booster_left()},
                {LeftBoosterRight, keyboard.left_booster_right()},
                {LeftBoosterButton, keyboard.left_booster_button()},
                {RightBoosterUp, keyboard.right_booster_up()},
                {RightBoosterDown, keyboard.right_booster_down()},
                {RightBoosterLeft, keyboard.right_booster_left()},
                {RightBoosterRight, keyboard.right_booster_right()},
                {RightBoosterButton, keyboard.right_booster_button()},
                {Test, keyboard.test()},
                {Service1, keyboard.service1()},
                {Service2, keyboard.service2()},
                {Service3, keyboard.service3()},
                {P1Start, keyboard.p1_start()},
                {P2Start, keyboard.p2_start()},
                {P2Service, keyboard.p2_service()},
            };
        }
    } // namespace

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

            if (document.input_schema_version() != kInputSchemaVersion)
            {
                errors.push_back({
                    .path = ConfigPath{"input_schema_version"},
                    .code = ConfigErrorCode::unsupported_value,
                    .message = "expected current input schema version",
                });
            }
            const bool poll_valid = ValidateLeaf<InputPollValidator>(
                document.input_poll_hz(),
                ConfigPath{"input_poll_hz"},
                ConfigErrorCode::unsupported_value,
                "expected one of 125, 250, 500, or 1000",
                errors);
            const bool mode_valid =
                document.input_mode() == input::InputMode::Keyboard ||
                document.input_mode() == input::InputMode::Controller;
            if (!mode_valid)
            {
                errors.push_back({
                    .path = ConfigPath{"input_mode"},
                    .code = ConfigErrorCode::unsupported_value,
                    .message = "unsupported input mode",
                });
            }
            if (document.gameplay_input_style() !=
                input::GameplayInputStyle::Arcade &&
                document.gameplay_input_style() !=
                input::GameplayInputStyle::Switch)
            {
                errors.push_back({
                    .path = ConfigPath{"gameplay_input_style"},
                    .code = ConfigErrorCode::unsupported_value,
                    .message = "unsupported gameplay input style",
                });
            }
            const bool press_valid = ValidateLeaf<PercentValidator>(
                document.axis_press_threshold_percent(),
                ConfigPath{"axis_press_threshold_percent"},
                ConfigErrorCode::out_of_range,
                "expected a percentage from 0 through 100",
                errors);
            const bool release_valid = ValidateLeaf<PercentValidator>(
                document.axis_release_threshold_percent(),
                ConfigPath{"axis_release_threshold_percent"},
                ConfigErrorCode::out_of_range,
                "expected a percentage from 0 through 100",
                errors);
            if (press_valid && release_valid &&
                document.axis_release_threshold_percent() >=
                document.axis_press_threshold_percent())
            {
                errors.push_back({
                    .path = ConfigPath{"axis_release_threshold_percent"},
                    .code = ConfigErrorCode::incompatible_fields,
                    .message = "release threshold must be below press threshold",
                    .related_paths = {
                        ConfigPath{"axis_press_threshold_percent"},
                    },
                });
            }

            for (const auto& [name, key] :
                 KeyboardFields(document.keyboard()))
            {
                ValidateLeaf<PhysicalKeyValidator>(
                    key,
                    ConfigPath{"keyboard"}.Child(std::string{name}),
                    ConfigErrorCode::invalid_value,
                    "physical scan-code token must be nonzero",
                    errors);
            }

            const auto backend = document.controller().backend();
            const bool backend_valid =
                backend == input::ControllerBackend::XInput ||
                backend == input::ControllerBackend::RawHid;
            if (!backend_valid)
            {
                errors.push_back({
                    .path = ConfigPath{"controller", "backend"},
                    .code = ConfigErrorCode::unsupported_value,
                    .message = "unsupported controller backend",
                });
            }
            bool device_valid = false;
            if (backend == input::ControllerBackend::XInput)
            {
                const auto& id = document.controller().device_id();
                device_valid =
                    id == "0" || id == "1" || id == "2" || id == "3";
            }
            else if (backend == input::ControllerBackend::RawHid)
            {
                device_valid = !document.controller().device_id().empty();
            }
            if (backend_valid && !device_valid)
            {
                errors.push_back({
                    .path = ConfigPath{"controller", "device_id"},
                    .code = ConfigErrorCode::invalid_value,
                    .message = "device identity does not match the backend",
                    .related_paths = {ConfigPath{"controller", "backend"}},
                });
            }
            for (std::size_t index = 0;
                 index < document.controller().bindings().size();
                 ++index)
            {
                const auto& binding =
                    document.controller().bindings()[index];
                const auto binding_path =
                    ConfigPath{"controller", "bindings"}.Index(index);
                if (!input::IsGameplayAction(binding.action))
                {
                    errors.push_back({
                        .path = binding_path.Child("action"),
                        .code = ConfigErrorCode::invalid_value,
                        .message =
                        "controller actions must be gameplay actions",
                    });
                    continue;
                }
                const bool xinput_type = IsXInputType(binding.type);
                if (backend_valid &&
                    ((backend == input::ControllerBackend::XInput) !=
                        xinput_type))
                {
                    errors.push_back({
                        .path = binding_path.Child("type"),
                        .code = ConfigErrorCode::incompatible_fields,
                        .message =
                        "binding type does not match controller backend",
                        .related_paths = {
                            ConfigPath{"controller", "backend"},
                        },
                    });
                    continue;
                }
                if (!BindingFieldsValid(binding))
                {
                    errors.push_back({
                        .path = binding_path.Child("type"),
                        .code = ConfigErrorCode::invalid_value,
                        .message =
                        "binding fields do not match the selected type",
                    });
                }
            }

            const auto server_octets =
                nesys_service::ParseDottedDecimalIpv4(
                    document.nesys().server_ip());
            if (!server_octets)
            {
                errors.push_back({
                    .path = ConfigPath{"nesys", "server_ip"},
                    .code = ConfigErrorCode::invalid_value,
                    .message = "expected dotted-decimal IPv4",
                });
            }

            ValidateLeaf<RegistryDwordValidator>(
                document.registry().nesys().game_kind(),
                ConfigPath{"registry", "nesys", "game_kind"},
                ConfigErrorCode::out_of_range,
                "expected a registry DWORD value",
                errors);
            ValidateLeaf<RegistryDwordValidator>(
                document.registry().nesys().event_next_time(),
                ConfigPath{"registry", "nesys", "event_next_time"},
                ConfigErrorCode::out_of_range,
                "expected a registry DWORD value",
                errors);
            ValidateLeaf<RegistryDwordValidator>(
                document.registry().nesys().condition_time(),
                ConfigPath{"registry", "nesys", "condition_time"},
                ConfigErrorCode::out_of_range,
                "expected a registry DWORD value",
                errors);
            ValidateLeaf<RegistryLogLevelValidator>(
                document.registry().nesys().log_level(),
                ConfigPath{"registry", "nesys", "log_level"},
                ConfigErrorCode::out_of_range,
                "unsupported registry log level",
                errors);
            auto derived_paths = registry_config::DeriveNesysPaths(
                document.registry().system_path());
            if (!derived_paths)
            {
                errors.push_back({
                    .path = ConfigPath{"registry", "system_path"},
                    .code = ConfigErrorCode::invalid_path,
                    .message = derived_paths.error(),
                });
            }

            const auto log_level = document.logging().level();
            if (log_level != logging::LoaderLogLevel::Info &&
                log_level != logging::LoaderLogLevel::Debug &&
                log_level != logging::LoaderLogLevel::Verbose)
            {
                errors.push_back({
                    .path = ConfigPath{"logging", "level"},
                    .code = ConfigErrorCode::unsupported_value,
                    .message = "unsupported loader log level",
                });
            }

            const auto target_fps = static_cast<std::uint32_t>(
                document.experimental().target_fps());
            ValidateLeaf<TargetFpsValidator>(
                target_fps,
                ConfigPath{"experimental", "target_fps"},
                ConfigErrorCode::out_of_range,
                "expected an integer from 60 through 500",
                errors);

            const auto audio_backend =
                document.experimental().audio_backend();
            const bool audio_backend_valid =
                audio_backend == audio::AudioBackend::directsound ||
                audio_backend == audio::AudioBackend::wasapi_exclusive ||
                audio_backend == audio::AudioBackend::asio;
            if (!audio_backend_valid)
            {
                errors.push_back({
                    .path =
                    ConfigPath{"experimental", "audio_backend"},
                    .code = ConfigErrorCode::unsupported_value,
                    .message = "unsupported audio backend",
                });
            }
            if (audio_backend == audio::AudioBackend::wasapi_exclusive)
            {
                ValidateLeaf<NonZeroWasapiBufferValidator>(
                    static_cast<std::uint32_t>(
                        document.experimental()
                                .wasapi_exclusive_buffer_ms()),
                    ConfigPath{
                        "experimental",
                        "wasapi_exclusive_buffer_ms",
                    },
                    ConfigErrorCode::out_of_range,
                    "WASAPI buffer duration must be greater than zero",
                    errors);
            }
            if (audio_backend == audio::AudioBackend::asio)
            {
                if (document.experimental().asio_driver_name().empty())
                {
                    errors.push_back({
                        .path = ConfigPath{
                            "experimental",
                            "asio_driver_name",
                        },
                        .code = ConfigErrorCode::required_value,
                        .message = "ASIO requires a driver name",
                    });
                }
                else
                {
                    ValidateLeaf<AsioDriverTextValidator>(
                        document.experimental().asio_driver_name(),
                        ConfigPath{
                            "experimental",
                            "asio_driver_name",
                        },
                        ConfigErrorCode::invalid_encoding,
                        "ASIO driver name must be valid bounded UTF-8",
                        errors);
                }
                const auto frames = static_cast<std::uint32_t>(
                    document.experimental().asio_buffer_frames());
                if (frames == 0 ||
                    frames > static_cast<std::uint32_t>(LONG_MAX))
                {
                    errors.push_back({
                        .path = ConfigPath{
                            "experimental",
                            "asio_buffer_frames",
                        },
                        .code = ConfigErrorCode::out_of_range,
                        .message = "ASIO buffer frames are out of range",
                    });
                }
                const auto channel = static_cast<std::uint32_t>(
                    document.experimental().asio_output_base_channel());
                if (channel >
                    static_cast<std::uint32_t>(LONG_MAX - 1))
                {
                    errors.push_back({
                        .path = ConfigPath{
                            "experimental",
                            "asio_output_base_channel",
                        },
                        .code = ConfigErrorCode::out_of_range,
                        .message = "ASIO output channel is out of range",
                    });
                }
            }
            if (document.experimental().enable_absolute_time_judgement())
            {
                if (audio_backend_valid &&
                    audio_backend !=
                    audio::AudioBackend::wasapi_exclusive &&
                    audio_backend != audio::AudioBackend::asio)
                {
                    errors.push_back({
                        .path =
                        ConfigPath{"experimental", "audio_backend"},
                        .code = ConfigErrorCode::unmet_dependency,
                        .message =
                        "absolute judgement requires WASAPI or ASIO",
                        .related_paths = {
                            ConfigPath{
                                "experimental",
                                "enable_absolute_time_judgement",
                            },
                        },
                    });
                }
                if (poll_valid && document.input_poll_hz() != 1000)
                {
                    errors.push_back({
                        .path = ConfigPath{"input_poll_hz"},
                        .code = ConfigErrorCode::unmet_dependency,
                        .message =
                        "absolute judgement requires 1000 Hz input",
                        .related_paths = {
                            ConfigPath{
                                "experimental",
                                "enable_absolute_time_judgement",
                            },
                        },
                    });
                }
            }

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

            const bool absolute =
                document.experimental()
                        .enable_absolute_time_judgement();
            return ValidatedConfig{
                logging::LoggingSettings{log_level},
                input::InputSettings{
                    document.input_poll_hz(),
                    absolute,
                    document.input_mode(),
                    document.axis_press_threshold_percent(),
                    document.axis_release_threshold_percent(),
                    CompileKeyboard(document.keyboard()),
                    std::move(controller),
                },
                switch_input::SwitchInputSettings{
                    document.gameplay_input_style(),
                },
                audio::AudioSettings{
                    audio_backend,
                    std::move(audio_selection),
                    absolute,
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
