#include "Audio/AudioSettings.h"
#include "Config/ConfigCompiler.h"
#include "Config/ConfigDocument.h"
#include "Config/ConfigError.h"
#include "Input/Switch/SwitchInputSettings.h"
#include "Input/Types/InputSettings.h"
#include "Input/Win32/ControllerBindingEvaluator.h"
#include "Logging/LoggingSettings.h"
#include "Nesys/NesysSettings.h"
#include "Patches/AbsoluteJudgement/JudgementSettings.h"
#include "Patches/Framerate/FramerateSettings.h"
#include "Rfid/FeatureSettings.h"
#include "SystemPath/SystemPathSettings.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_copy_constructible_v<gc::audio::AudioSettings>);
static_assert(std::is_move_constructible_v<gc::input::InputSettings>);
static_assert(
    std::is_move_constructible_v<gc::switch_input::SwitchInputSettings>);
static_assert(std::is_copy_constructible_v<gc::logging::LoggingSettings>);
static_assert(std::is_move_constructible_v<gc::nesys_service::NesysSettings>);
static_assert(
    std::is_copy_constructible_v<gc::absolute_judgement::JudgementSettings>);
static_assert(std::is_copy_constructible_v<gc::framerate::FramerateSettings>);
static_assert(std::is_copy_constructible_v<gc::rfid::FeatureSettings>);
static_assert(std::is_move_constructible_v<gc::system_path::SystemPathSettings>);

namespace
{
    int g_failures{};

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    std::string ReadDistributedConfig()
    {
        std::ifstream input{GC_TEST_CONFIG_PATH, std::ios::binary};
        Expect(input.is_open(), "distributed config opens");
        if (!input)
        {
            return {};
        }
        return {
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{},
        };
    }

    void DistributedConfigStrictlyParses()
    {
        const auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "distributed config strictly parses");
    }

    void SemanticInvalidityDoesNotDestroyDocumentShape()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "distributed config parses for draft test");
        if (!parsed)
        {
            return;
        }

        parsed->document.experimental().target_fps = 59;
        const auto serialized =
            gc::config::SerializeConfigDocument(parsed->document);
        Expect(
            serialized.has_value(),
            "semantic-invalid draft still serializes");
        if (!serialized)
        {
            return;
        }

        const auto reparsed = gc::config::ParseConfigDocument(*serialized);
        Expect(
            reparsed.has_value(),
            "semantic invalidity does not destroy document shape");
    }

    using ErrorKey =
    std::pair<std::string, gc::config::ConfigErrorCode>;

    std::vector<ErrorKey> ErrorKeys(
        const gc::config::ConfigErrors& errors)
    {
        std::vector<ErrorKey> keys;
        keys.reserve(errors.size());
        for (const auto& error : errors)
        {
            keys.emplace_back(error.path.Render(), error.code);
        }
        return keys;
    }

    class ActiveXInputAView final : public gc::input::ControllerStateView
    {
    public:
        [[nodiscard]] const gc::input::ControllerIdentity&
        identity() const noexcept override
        {
            return identity_;
        }

        [[nodiscard]] std::span<
            const gc::input::ControllerControlDescriptor>
        controls() const noexcept override
        {
            return {};
        }

        [[nodiscard]] std::optional<double> Activation(
            const gc::input::DigitalControlBinding& binding)
        const noexcept override
        {
            ++activation_calls_;
            if (binding.type ==
                gc::input::DigitalControlType::XInputButton &&
                binding.control == gc::input::XInputControl::A)
            {
                return 1.0;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::int32_t> RawValue(
            const gc::input::DigitalControlBinding&) const noexcept override
        {
            return std::nullopt;
        }

        [[nodiscard]] int activation_calls() const noexcept
        {
            return activation_calls_;
        }

    private:
        gc::input::ControllerIdentity identity_{
            .backend = gc::input::ControllerBackend::XInput,
            .device_id = "2",
        };
        mutable int activation_calls_{};
    };

    void CompilerReturnsEveryIndependentErrorInDocumentOrder()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "multi-error source document parses");
        if (!parsed)
        {
            return;
        }

        auto& document = parsed->document;
        document.input_poll_hz = 333;
        document.axis_press_threshold_percent = 101;
        document.keyboard().test = {
            .make_code = 0,
            .prefix = gc::input::ScanCodePrefix::None,
        };
        document.nesys().server_ip = "999.1.1.1";
        document.experimental().target_fps = 59;

        const auto result =
            gc::config::ConfigCompiler::Compile(document);
        Expect(!result.has_value(), "multi-error document is rejected");
        if (!result)
        {
            Expect(
                ErrorKeys(result.error()) == std::vector<ErrorKey>{
                    {
                        "input_poll_hz",
                        gc::config::ConfigErrorCode::unsupported_value,
                    },
                    {
                        "axis_press_threshold_percent",
                        gc::config::ConfigErrorCode::out_of_range,
                    },
                    {
                        "keyboard.test",
                        gc::config::ConfigErrorCode::invalid_value,
                    },
                    {
                        "nesys.server_ip",
                        gc::config::ConfigErrorCode::invalid_value,
                    },
                    {
                        "experimental.target_fps",
                        gc::config::ConfigErrorCode::out_of_range,
                    },
                },
                "compiler returns every independent error in declaration order");
        }
    }

    void CompilerChecksSelectedWasapiBuffer()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "WASAPI source document parses");
        if (!parsed)
        {
            return;
        }
        parsed->document.experimental().audio_backend =
            gc::audio::AudioBackend::wasapi_exclusive;
        parsed->document.experimental().wasapi_exclusive_buffer_ms = 0;

        const auto result =
            gc::config::ConfigCompiler::Compile(parsed->document);
        Expect(!result.has_value(), "zero WASAPI buffer is rejected");
        if (!result)
        {
            Expect(
                ErrorKeys(result.error()) == std::vector<ErrorKey>{
                    {
                        "experimental.wasapi_exclusive_buffer_ms",
                        gc::config::ConfigErrorCode::out_of_range,
                    },
                },
                "WASAPI buffer error has the stable path and code");
        }
    }

    void CompilerChecksAsioFallbackWasapiBuffer()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "ASIO fallback source document parses");
        if (!parsed)
        {
            return;
        }
        auto& experimental = parsed->document.experimental();
        experimental.audio_backend = gc::audio::AudioBackend::asio;
        experimental.wasapi_exclusive_buffer_ms = 0;
        experimental.asio_driver_name = "Fallback ASIO";
        experimental.asio_buffer_frames = 192;
        experimental.asio_output_base_channel = 0;

        const auto result =
            gc::config::ConfigCompiler::Compile(parsed->document);
        Expect(!result.has_value(), "zero ASIO fallback buffer is rejected");
        if (!result)
        {
            Expect(
                ErrorKeys(result.error()) == std::vector<ErrorKey>{
                    {
                        "experimental.wasapi_exclusive_buffer_ms",
                        gc::config::ConfigErrorCode::out_of_range,
                    },
                },
                "ASIO fallback buffer error has the stable path and code");
        }
    }

    void DependentRulesRunOnlyAfterTheirLeavesPass()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "dependency source document parses");
        if (!parsed)
        {
            return;
        }
        parsed->document.input_poll_hz = 333;
        parsed->document.experimental().enable_absolute_time_judgement = true;
        parsed->document.experimental().audio_backend =
            gc::audio::AudioBackend::directsound;

        const auto result =
            gc::config::ConfigCompiler::Compile(parsed->document);
        Expect(!result.has_value(), "invalid dependencies are rejected");
        if (!result)
        {
            Expect(
                ErrorKeys(result.error()) == std::vector<ErrorKey>{
                    {
                        "input_poll_hz",
                        gc::config::ConfigErrorCode::unsupported_value,
                    },
                    {
                        "experimental.audio_backend",
                        gc::config::ConfigErrorCode::unmet_dependency,
                    },
                },
                "invalid poll leaf suppresses only its dependent error");
        }
    }

    void CompilerProducesConcreteAudioAndControllerAlternatives()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "variant source document parses");
        if (!parsed)
        {
            return;
        }
        auto& document = parsed->document;
        document.experimental().audio_backend =
            gc::audio::AudioBackend::asio;
        document.experimental().asio_driver_name = "Test ASIO";
        document.experimental().asio_buffer_frames = 256;
        document.experimental().asio_output_base_channel = 4;
        document.controller().backend =
            gc::input::ControllerBackend::XInput;
        document.controller().device_id = "2";
        document.controller().bindings = {
            {
                .action = gc::input::LogicalAction::LeftBoosterLeft,
                .type = gc::input::DigitalControlType::XInputAxis,
                .control = gc::input::XInputControl::LeftX,
                .direction = gc::input::ControlDirection::Negative,
            },
        };

        const auto result =
            gc::config::ConfigCompiler::Compile(document);
        Expect(result.has_value(), "valid concrete alternatives compile");
        if (!result)
        {
            return;
        }

        const auto* asio = std::get_if<gc::audio::AsioSettings>(
            &result->audio().selection());
        Expect(asio != nullptr, "ASIO selection has concrete settings");
        if (asio != nullptr)
        {
            Expect(asio->driver_name() == "Test ASIO", "ASIO driver is owned");
            Expect(asio->buffer_frames() == 256, "ASIO buffer is preserved");
            Expect(
                asio->output_base_channel() == 4,
                "ASIO output channel is preserved");
        }

        const auto* xinput =
            std::get_if<gc::input::XInputControllerSettings>(
                &result->input().controller());
        Expect(xinput != nullptr, "XInput selection has concrete settings");
        if (xinput != nullptr)
        {
            Expect(xinput->slot() == 2, "XInput slot is parsed");
            Expect(
                xinput->bindings().size() == 1 &&
                std::holds_alternative<gc::input::XInputAxisBinding>(
                    xinput->bindings().front()),
                "axis binding has its concrete alternative");
        }
    }

    void FormatterIncludesCompleteCompilerErrorsInOrder()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "formatter source document parses");
        if (!parsed)
        {
            return;
        }

        parsed->document.input_poll_hz = 333;
        parsed->document.experimental().target_fps = 59;
        const auto result =
            gc::config::ConfigCompiler::Compile(parsed->document);
        Expect(!result.has_value(), "formatter source has compiler errors");
        if (!result)
        {
            Expect(
                result.error().size() == 2,
                "formatter source has exactly two independent errors");
        }
        if (result || result.error().size() != 2)
        {
            return;
        }

        const auto formatted = gc::config::FormatConfigErrors(result.error());
        const auto first = result.error()[0].path.Render() + ": " +
            result.error()[0].message;
        const auto second = result.error()[1].path.Render() + ": " +
            result.error()[1].message;
        const auto first_position = formatted.find(first);
        const auto second_position = formatted.find(second);
        Expect(
            first_position != std::string::npos,
            "formatter includes the first compiler path and message");
        Expect(
            second_position != std::string::npos,
            "formatter includes the second compiler path and message");
        Expect(
            first_position < second_position,
            "formatter preserves compiler error order");
    }

    void CompiledAudioSettingsOwnAsioFallback()
    {
        std::optional<gc::audio::AudioSettings> owned_settings;
        {
            auto parsed =
                gc::config::ParseConfigDocument(ReadDistributedConfig());
            Expect(parsed.has_value(), "audio ownership document parses");
            if (!parsed)
            {
                return;
            }

            auto& experimental = parsed->document.experimental();
            experimental.audio_backend = gc::audio::AudioBackend::asio;
            experimental.wasapi_exclusive_buffer_ms = 7;
            experimental.asio_driver_name = "Owned ASIO";
            experimental.asio_buffer_frames = 192;
            experimental.asio_output_base_channel = 6;
            auto compiled =
                gc::config::ConfigCompiler::Compile(parsed->document);
            Expect(compiled.has_value(), "audio ownership document compiles");
            if (!compiled)
            {
                return;
            }
            owned_settings.emplace(compiled->audio());
        }

        const auto* asio = std::get_if<gc::audio::AsioSettings>(
            &owned_settings->selection());
        Expect(asio != nullptr, "owned settings retain ASIO alternative");
        if (asio == nullptr)
        {
            return;
        }

        Expect(asio->driver_name() == "Owned ASIO", "owned ASIO driver survives");
        Expect(asio->buffer_frames() == 192, "owned ASIO buffer survives");
        Expect(
            asio->output_base_channel() == 6,
            "owned ASIO output channel survives");
        Expect(
            asio->wasapi_fallback_buffer_ms() == 7,
            "owned ASIO WASAPI fallback buffer survives");
    }

    void CompiledInputSettingsOwnControllerBindings()
    {
        std::optional<gc::input::InputSettings> owned_settings;
        {
            auto parsed =
                gc::config::ParseConfigDocument(ReadDistributedConfig());
            Expect(parsed.has_value(), "input ownership document parses");
            if (!parsed)
            {
                return;
            }

            auto& controller = parsed->document.controller();
            controller.backend = gc::input::ControllerBackend::XInput;
            controller.device_id = "2";
            controller.bindings = {
                {
                    .action = gc::input::LogicalAction::LeftBoosterButton,
                    .type = gc::input::DigitalControlType::XInputButton,
                    .control = gc::input::XInputControl::A,
                },
            };
            auto compiled =
                gc::config::ConfigCompiler::Compile(parsed->document);
            Expect(compiled.has_value(), "input ownership document compiles");
            if (!compiled)
            {
                return;
            }
            owned_settings.emplace(compiled->input());
        }

        const auto* xinput =
            std::get_if<gc::input::XInputControllerSettings>(
                &owned_settings->controller());
        Expect(xinput != nullptr, "owned settings retain XInput alternative");
        if (xinput == nullptr)
        {
            return;
        }

        auto evaluator = gc::input::ControllerBindingEvaluator::Create(
            xinput->bindings(),
            owned_settings->press_percent(),
            owned_settings->release_percent());
        Expect(evaluator.has_value(), "owned bindings create the evaluator");
        if (!evaluator)
        {
            return;
        }

        ActiveXInputAView view;
        const auto states = evaluator->Update(view);
        Expect(
            states.size() == 1 && states.front() == 1,
            "owned binding evaluates after document and grouping destruction");
        Expect(
            view.activation_calls() == 1,
            "typed binding reaches the production raw state seam once");
    }

    void BackendMismatchReportsOnePrimaryBindingError()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "mismatch source document parses");
        if (!parsed)
        {
            return;
        }
        auto& controller = parsed->document.controller();
        controller.backend = gc::input::ControllerBackend::RawHid;
        controller.device_id = "raw-device";
        controller.bindings = {
            {
                .action = gc::input::LogicalAction::LeftBoosterLeft,
                .type = gc::input::DigitalControlType::XInputAxis,
                .control = gc::input::XInputControl::LeftX,
                .direction = gc::input::ControlDirection::Negative,
            },
        };

        const auto result =
            gc::config::ConfigCompiler::Compile(parsed->document);
        Expect(!result.has_value(), "backend/type mismatch is rejected");
        if (!result)
        {
            Expect(result.error().size() == 1, "mismatch has one primary error");
            if (result.error().size() == 1)
            {
                const auto& error = result.error().front();
                Expect(
                    error.path.Render() == "controller.bindings[0].type",
                    "mismatch points at binding type");
                Expect(
                    error.related_paths.size() == 1 &&
                    error.related_paths.front().Render() ==
                    "controller.backend",
                    "mismatch relates the selected backend");
            }
        }
    }
} // namespace

int main()
{
    DistributedConfigStrictlyParses();
    SemanticInvalidityDoesNotDestroyDocumentShape();
    CompilerReturnsEveryIndependentErrorInDocumentOrder();
    CompilerChecksSelectedWasapiBuffer();
    CompilerChecksAsioFallbackWasapiBuffer();
    DependentRulesRunOnlyAfterTheirLeavesPass();
    CompilerProducesConcreteAudioAndControllerAlternatives();
    FormatterIncludesCompleteCompilerErrorsInOrder();
    CompiledAudioSettingsOwnAsioFallback();
    CompiledInputSettingsOwnControllerBindings();
    BackendMismatchReportsOnePrimaryBindingError();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
