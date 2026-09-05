#include "Audio/AudioSettings.h"
#include "Config/ConfigCompiler.h"
#include "Config/ConfigDocument.h"
#include "Config/ConfigError.h"
#include "Config/DeclaredEnum.h"
#include "Input/Switch/SwitchInputSettings.h"
#include "Input/Types/InputSettings.h"
#include "Input/Types/PhysicalKey.h"
#include "Input/Win32/ControllerBindingEvaluator.h"
#include "Logging/LoggingSettings.h"
#include "Nesys/NesysSettings.h"
#include "Patches/AbsoluteJudgement/JudgementSettings.h"
#include "Patches/Framerate/FramerateSettings.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenSettings.h"
#include "Rfid/FeatureSettings.h"
#include "SystemPath/SystemPathSettings.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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
static_assert(std::is_copy_constructible_v<
              gc::windowed_widescreen::WindowedWidescreenSettings>);

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


    std::string WithEnumToken(
        std::string_view field, std::string_view token)
    {
        auto text = ReadDistributedConfig();
        const auto marker = "\n" + std::string{field} + " = '";
        const auto field_start = text.find(marker);
        Expect(field_start != std::string::npos, "enum field exists");
        if (field_start == std::string::npos)
        {
            return {};
        }
        Expect(text.find(marker, field_start + 1) == std::string::npos,
               "exactly one enum field is replaced");
        const auto value_start = field_start + marker.size();
        const auto value_end = text.find('\'', value_start);
        Expect(value_end != std::string::npos, "quoted enum token ends");
        if (value_end == std::string::npos)
        {
            return {};
        }
        text.replace(value_start, value_end - value_start, token);
        return text;
    }

    void CompilerRejectsUndeclaredNumericEnumTokens()
    {
        const std::pair<std::string_view, std::string_view> cases[]{
            {"input_mode", "input_mode"},
            {"gameplay_input_style", "gameplay_input_style"},
            {"backend", "controller.backend"},
            {"country", "registry.game.country"},
            {"level", "logging.level"},
            {"widescreen_hud_placement", "experimental.widescreen_hud_placement"},
            {"audio_backend", "experimental.audio_backend"},
        };
        for (const auto& [field, path] : cases)
        {
            const auto parsed =
                gc::config::ParseConfigDocument(WithEnumToken(field, "99"));
            Expect(parsed.has_value(), "numeric enum token parses at syntax layer");
            if (!parsed)
            {
                continue;
            }
            const auto compiled =
                gc::config::ConfigCompiler::Compile(parsed->document);
            Expect(!compiled.has_value(), "undeclared numeric enum is rejected");
            if (!compiled)
            {
                Expect(
                    ErrorKeys(compiled.error()) == std::vector<ErrorKey>{
                        {std::string{path}, gc::config::ConfigErrorCode::unsupported_value},
                    },
                    "undeclared enum reports its production field and code");
            }
        }
    }

    template <class Enum>
    void CompilerAcceptsEveryDeclaredEnum(std::string_view field)
    {
        for (const auto& [name, value] : gc::config::DeclaredEnumValues<Enum>())
        {
            // Names and declared numeric values must agree at the compiler
            // boundary; no test-local enumerator list is maintained.
            const auto numeric = std::to_string(
                static_cast<std::underlying_type_t<Enum>>(value));
            for (const auto token : {name, std::string_view{numeric}})
            {
                auto parsed =
                    gc::config::ParseConfigDocument(WithEnumToken(field, token));
                Expect(parsed.has_value(), "declared enum token parses");
                if (!parsed)
                {
                    continue;
                }
                parsed->document.experimental().asio_driver_name = "Contract driver";
                parsed->document.experimental().asio_buffer_frames = 512;
                // Binding compatibility is a separate domain rule. Both
                // controller backends accept an empty binding set and ID "0".
                if constexpr (std::is_same_v<Enum, gc::input::ControllerBackend>)
                {
                    parsed->document.controller().bindings().clear();
                }
                const auto compiled =
                    gc::config::ConfigCompiler::Compile(parsed->document);
                Expect(compiled.has_value(), "every declared enum compiles");
                if (!compiled)
                {
                    std::cerr << field << " = " << token << '\n';
                    for (const auto& error : compiled.error())
                    {
                        std::cerr << error.path.Render() << ": " << error.message << '\n';
                    }
                }
            }
        }
    }

    void DeclaredConfigEnumsAgreeWithCompiler()
    {
        CompilerAcceptsEveryDeclaredEnum<gc::input::InputMode>("input_mode");
        CompilerAcceptsEveryDeclaredEnum<gc::input::GameplayInputStyle>("gameplay_input_style");
        CompilerAcceptsEveryDeclaredEnum<gc::input::ControllerBackend>("backend");
        CompilerAcceptsEveryDeclaredEnum<GameCountry>("country");
        CompilerAcceptsEveryDeclaredEnum<gc::logging::LoaderLogLevel>("level");
        CompilerAcceptsEveryDeclaredEnum<gc::windowed_widescreen::GameplayHudPlacement>(
            "widescreen_hud_placement");
        CompilerAcceptsEveryDeclaredEnum<gc::audio::AudioBackend>("audio_backend");
    }

    void PhysicalKeyCodecPreservesExternalTokens()
    {
        for (const auto token : {"sc:001e", "e0:0048", "e1:001d"})
        {
            const auto key = gc::input::ParsePhysicalKey(token);
            Expect(key.has_value(), "physical key token parses");
            if (key)
            {
                Expect(gc::input::FormatPhysicalKey(*key) == token,
                       "physical key token round trips exactly");
            }
        }
        for (const auto token : {"zz:001e", "SC:001e", "sc:00xz", "sc:0000", "sc:001"})
        {
            Expect(!gc::input::ParsePhysicalKey(token),
                   "invalid prefix, hex, zero or shape is rejected");
        }
        Expect(gc::input::FormatPhysicalKey({
                   0x1e, static_cast<gc::input::ScanCodePrefix>(99)}) == "sc:001e",
               "unknown formatter prefix retains the sc fallback");
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

    void CompilerIgnoresUnselectedWasapiBufferForAsio()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "ASIO selection source document parses");
        if (!parsed)
        {
            return;
        }
        auto& experimental = parsed->document.experimental();
        experimental.audio_backend = gc::audio::AudioBackend::asio;
        experimental.wasapi_exclusive_buffer_ms = 0;
        experimental.asio_driver_name = "Selected ASIO";
        experimental.asio_buffer_frames = 192;
        experimental.asio_output_base_channel = 0;

        const auto result =
            gc::config::ConfigCompiler::Compile(parsed->document);
        Expect(
            result.has_value(),
            "ASIO does not depend on an unselected WASAPI buffer");
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

    void CompiledAudioSettingsOwnAsioSelection()
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

    void CompiledNesysAndRfidSettingsOwnDerivedValues()
    {
        std::optional<gc::nesys_service::NesysSettings> owned_nesys;
        std::optional<gc::rfid::FeatureSettings> owned_rfid;
        {
            auto parsed =
                gc::config::ParseConfigDocument(ReadDistributedConfig());
            Expect(parsed.has_value(), "NESYS ownership document parses");
            if (!parsed)
            {
                return;
            }

            parsed->document.nesys().server_ip = "010.020.030.040";
            parsed->document.registry().enabled = true;
            parsed->document.registry().system_path = ".\\owned-system";
            parsed->document.keyboard().card_read = gc::input::PhysicalKey{
                .make_code = 0x2E,
                .prefix = gc::input::ScanCodePrefix::E0,
            };
            parsed->document.experimental()
                  .enable_testmode_storage_redirect = true;

            auto compiled =
                gc::config::ConfigCompiler::Compile(parsed->document);
            Expect(compiled.has_value(), "NESYS ownership document compiles");
            if (!compiled)
            {
                return;
            }
            owned_nesys.emplace(compiled->nesys());
            owned_rfid.emplace(compiled->rfid());
        }

        const auto& server = owned_nesys->server_address();
        Expect(
            server.octets() == gc::nesys_service::Ipv4Octets{10, 20, 30, 40},
            "owned NESYS IPv4 octets survive");
        Expect(server.ansi() == "10.20.30.40", "owned NESYS ANSI address survives");
        Expect(
            server.wide() == L"10.20.30.40",
            "owned NESYS wide address survives");

        const auto& registry = owned_nesys->registry_override();
        Expect(registry.has_value(), "owned NESYS registry values survive");
        if (registry)
        {
            Expect(
                registry->news_path() == ".\\owned-system\\DUA\\news",
                "owned NESYS news path survives");
            Expect(
                registry->event_path() == ".\\owned-system\\DUA\\event",
                "owned NESYS event path survives");
            Expect(
                registry->log_path() == ".\\owned-system\\CmdFile\\log",
                "owned NESYS log path survives");
        }

        Expect(
            owned_rfid->card_read_key() == gc::input::PhysicalKey{
                .make_code = 0x2E,
                .prefix = gc::input::ScanCodePrefix::E0,
            },
            "owned RFID key survives");
        Expect(
            owned_rfid->testmode_storage_redirect_enabled(),
            "owned RFID storage policy survives");
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

    void CompilerOwnsWindowedWidescreenSettings()
    {
        auto parsed =
            gc::config::ParseConfigDocument(ReadDistributedConfig());
        Expect(parsed.has_value(), "widescreen source document parses");
        if (!parsed)
        {
            return;
        }

        auto& experimental = parsed->document.experimental();
        experimental.enable_windowed_widescreen_stage = true;
        experimental.widescreen_window_width = 3840;
        experimental.widescreen_window_height = 1280;

        auto compiled = gc::config::ConfigCompiler::Compile(parsed->document);
        Expect(compiled.has_value(), "valid widescreen settings compile");
        if (!compiled)
        {
            return;
        }

        const auto owned = compiled->windowed_widescreen();
        experimental.enable_windowed_widescreen_stage = false;
        experimental.widescreen_window_width = 720;
        experimental.widescreen_window_height = 4096;

        Expect(owned.enabled(), "compiled widescreen enablement is owned");
        Expect(
            owned.output_width() == 3840,
            "compiled widescreen width is owned");
        Expect(
            owned.output_height() == 1280,
            "compiled widescreen height is owned");
    }

    void CompilerRejectsInvalidWindowedWidescreenSettings()
    {
        const auto expect_error = [](
            const auto mutate,
            const std::string_view expected_path,
            const gc::config::ConfigErrorCode expected_code,
            const std::string_view message)
        {
            auto parsed =
                gc::config::ParseConfigDocument(ReadDistributedConfig());
            Expect(parsed.has_value(), "invalid widescreen source parses");
            if (!parsed)
            {
                return;
            }
            mutate(parsed->document.experimental());
            const auto compiled =
                gc::config::ConfigCompiler::Compile(parsed->document);
            Expect(!compiled.has_value(), message);
            if (!compiled)
            {
                Expect(
                    ErrorKeys(compiled.error()) == std::vector<ErrorKey>{
                        {std::string{expected_path}, expected_code},
                    },
                    "invalid widescreen setting reports one stable error");
            }
        };

        expect_error(
            [](auto& value) { value.widescreen_window_width = 719; },
            "experimental.widescreen_window_width",
            gc::config::ConfigErrorCode::out_of_range,
            "widescreen width below 720 is rejected");
        expect_error(
            [](auto& value) { value.widescreen_window_height = 1279; },
            "experimental.widescreen_window_height",
            gc::config::ConfigErrorCode::out_of_range,
            "widescreen height below 1280 is rejected");
        expect_error(
            [](auto& value) { value.widescreen_window_height = 1281; },
            "experimental.widescreen_window_height",
            gc::config::ConfigErrorCode::out_of_range,
            "widescreen height above 1280 is rejected");
        expect_error(
            [](auto& value)
            {
                value.widescreen_window_width =
                    std::numeric_limits<unsigned long>::max();
            },
            "experimental.widescreen_window_width",
            gc::config::ConfigErrorCode::out_of_range,
            "widescreen width beyond native signed range is rejected");
    }
} // namespace

int main()
{
    DistributedConfigStrictlyParses();
    CompilerRejectsUndeclaredNumericEnumTokens();
    DeclaredConfigEnumsAgreeWithCompiler();
    PhysicalKeyCodecPreservesExternalTokens();
    SemanticInvalidityDoesNotDestroyDocumentShape();
    CompilerReturnsEveryIndependentErrorInDocumentOrder();
    CompilerChecksSelectedWasapiBuffer();
    CompilerIgnoresUnselectedWasapiBufferForAsio();
    DependentRulesRunOnlyAfterTheirLeavesPass();
    CompilerProducesConcreteAudioAndControllerAlternatives();
    FormatterIncludesCompleteCompilerErrorsInOrder();
    CompiledAudioSettingsOwnAsioSelection();
    CompiledInputSettingsOwnControllerBindings();
    CompiledNesysAndRfidSettingsOwnDerivedValues();
    BackendMismatchReportsOnePrimaryBindingError();
    CompilerOwnsWindowedWidescreenSettings();
    CompilerRejectsInvalidWindowedWidescreenSettings();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
