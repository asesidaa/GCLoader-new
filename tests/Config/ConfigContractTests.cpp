#include "Audio/AudioSettings.h"
#include "Config/ConfigDocument.h"
#include "Input/Switch/SwitchInputSettings.h"
#include "Input/Types/InputSettings.h"
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
#include <string>
#include <string_view>
#include <type_traits>

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
} // namespace

int main()
{
    DistributedConfigStrictlyParses();
    SemanticInvalidityDoesNotDestroyDocumentShape();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
