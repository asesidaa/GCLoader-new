#include "Config/ConfigDocument.h"

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#ifndef GC_TEST_CONFIG_PATH
#error GC_TEST_CONFIG_PATH must name the distributed config.toml
#endif

namespace {

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

std::string ReadDistributedConfig() {
    std::ifstream input{GC_TEST_CONFIG_PATH, std::ios::binary};
    if (!input) {
        std::cerr << "Could not open distributed config: "
                  << GC_TEST_CONFIG_PATH << '\n';
        std::exit(2);
    }
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

std::size_t FindAssignment(
    const std::string& text,
    std::string_view key) {
    const std::string marker = std::string{key} + " = ";
    std::size_t position = text.find(marker);
    while (position != std::string::npos &&
           position != 0 &&
           text[position - 1] != '\n') {
        position = text.find(marker, position + marker.size());
    }
    if (position == std::string::npos) {
        std::cerr << "Config fixture does not contain assignment for "
                  << key << '\n';
        std::exit(2);
    }
    return position;
}

std::string RemoveAssignment(
    std::string text,
    std::string_view key) {
    const auto position = FindAssignment(text, key);
    auto end = text.find('\n', position);
    if (end == std::string::npos) {
        end = text.size();
    } else {
        ++end;
    }
    text.erase(position, end - position);
    return text;
}

std::string InsertAfterLine(
    std::string text,
    std::string_view line,
    std::string_view insertion) {
    const auto position = text.find(line);
    if (position == std::string::npos) {
        std::cerr << "Config fixture does not contain line: "
                  << line << '\n';
        std::exit(2);
    }
    const auto line_end = text.find('\n', position + line.size());
    if (line_end == std::string::npos) {
        std::cerr << "Config fixture line has no terminator: "
                  << line << '\n';
        std::exit(2);
    }
    const std::string_view newline =
        line_end != 0 && text[line_end - 1] == '\r'
            ? "\r\n"
            : "\n";
    text.insert(
        line_end + 1,
        std::string{insertion} + std::string{newline});
    return text;
}

std::string LegacyConfig(
    const std::string& distributed,
    std::string_view news,
    std::string_view event,
    std::string_view log) {
    auto legacy = RemoveAssignment(distributed, "system_path");
    return InsertAfterLine(
        std::move(legacy),
        "[registry.nesys]",
        "news_path = '" + std::string{news} + "'\n" +
            "event_path = '" + std::string{event} + "'\n" +
            "log_path = '" + std::string{log} + "'");
}

std::string LegacyAudioConfig(
    const std::string& distributed,
    bool enable_wasapi) {
    auto legacy = RemoveAssignment(distributed, "audio_backend");
    legacy = RemoveAssignment(std::move(legacy), "asio_driver_name");
    legacy = RemoveAssignment(std::move(legacy), "asio_buffer_frames");
    legacy = RemoveAssignment(
        std::move(legacy),
        "asio_output_base_channel");
    return InsertAfterLine(
        std::move(legacy),
        "[experimental]",
        std::string{"enable_wasapi_exclusive_audio = "} +
            (enable_wasapi ? "true" : "false"));
}

int ExpectDocumentFailure(
    std::string_view text,
    std::string_view expected_error,
    std::string_view name) {
    const auto result =
        gc::config::ParseAndValidateInputConfigDocument(text);
    return Expect(
        !result &&
            result.error().find(expected_error) != std::string::npos,
        name);
}

struct AtomicFake {
    bool fail_write{};
    bool fail_replace{};
    int writes{};
    int replaces{};
    int removes{};
    std::filesystem::path destination;
    std::filesystem::path temporary;
    std::filesystem::path removed;
    std::string serialized;
};

std::expected<void, std::string> FakeWrite(
    void* context,
    const std::filesystem::path& temporary,
    std::string_view serialized) noexcept {
    auto& fake = *static_cast<AtomicFake*>(context);
    ++fake.writes;
    fake.temporary = temporary;
    fake.serialized = serialized;
    if (fake.fail_write) {
        return std::unexpected("injected write failure");
    }
    return {};
}

std::expected<void, std::string> FakeReplace(
    void* context,
    const std::filesystem::path& destination,
    const std::filesystem::path& replacement) noexcept {
    auto& fake = *static_cast<AtomicFake*>(context);
    ++fake.replaces;
    fake.destination = destination;
    fake.temporary = replacement;
    if (fake.fail_replace) {
        return std::unexpected("injected replacement failure");
    }
    return {};
}

void FakeRemove(
    void* context,
    const std::filesystem::path& temporary) noexcept {
    auto& fake = *static_cast<AtomicFake*>(context);
    ++fake.removes;
    fake.removed = temporary;
}

gc::config::AtomicConfigWriteActions AtomicActions(
    AtomicFake& fake) {
    return {
        .context = &fake,
        .write = &FakeWrite,
        .replace = &FakeReplace,
        .remove = &FakeRemove,
    };
}

} // namespace

int main() {
    using gc::config::AudioBackend;

    int failures = 0;
    const std::string distributed = ReadDistributedConfig();

    const auto canonical =
        gc::config::ParseAndValidateInputConfigDocument(distributed);
    failures += Expect(
        canonical && !canonical->migrations.any(),
        "new audio schema requires no migration");

    const auto legacy_directsound =
        gc::config::ParseAndValidateInputConfigDocument(
            LegacyAudioConfig(distributed, false));
    failures += Expect(
        legacy_directsound &&
            legacy_directsound->migrations.audio_backend &&
            legacy_directsound->migrations.any() &&
            legacy_directsound->config.experimental().audio_backend() ==
                AudioBackend::directsound &&
            legacy_directsound->config.experimental()
                .wasapi_exclusive_buffer_ms() == 10 &&
            legacy_directsound->config.experimental()
                .asio_driver_name().empty() &&
            legacy_directsound->config.experimental()
                .asio_buffer_frames() == 0 &&
            legacy_directsound->config.experimental()
                .asio_output_base_channel() == 0,
        "legacy false audio Boolean migrates to inactive DirectSound schema");

    const auto legacy_wasapi =
        gc::config::ParseAndValidateInputConfigDocument(
            LegacyAudioConfig(distributed, true));
    failures += Expect(
        legacy_wasapi && legacy_wasapi->migrations.audio_backend &&
            legacy_wasapi->config.experimental().audio_backend() ==
                AudioBackend::wasapi_exclusive &&
            legacy_wasapi->config.experimental()
                .wasapi_exclusive_buffer_ms() == 10 &&
            legacy_wasapi->config.experimental()
                .asio_driver_name().empty() &&
            legacy_wasapi->config.experimental()
                .asio_buffer_frames() == 0 &&
            legacy_wasapi->config.experimental()
                .asio_output_base_channel() == 0,
        "legacy true audio Boolean migrates to WASAPI and inactive ASIO defaults");

    failures += ExpectDocumentFailure(
        InsertAfterLine(
            distributed,
            "[experimental]",
            "enable_wasapi_exclusive_audio = true"),
        "both audio_backend and legacy",
        "new and legacy audio selectors are ambiguous");
    failures += ExpectDocumentFailure(
        RemoveAssignment(distributed, "asio_buffer_frames"),
        "",
        "new audio schema remains strict when one ASIO field is missing");
    auto non_boolean_legacy = LegacyAudioConfig(distributed, false);
    const auto legacy_value = FindAssignment(
        non_boolean_legacy,
        "enable_wasapi_exclusive_audio");
    const auto value_begin = legacy_value +
        std::string_view{"enable_wasapi_exclusive_audio = "}.size();
    non_boolean_legacy.replace(value_begin, 5, "'no'");
    failures += ExpectDocumentFailure(
        non_boolean_legacy,
        "legacy enable_wasapi_exclusive_audio must be a Boolean",
        "legacy audio selector must be Boolean");

    const auto migrated =
        gc::config::ParseAndValidateInputConfigDocument(
            LegacyConfig(
                distributed,
                "D:\\system\\DUA\\news",
                "D:\\system\\DUA\\event",
                "D:\\system\\CmdFile\\log"));
    failures += Expect(
        migrated && migrated->migrations.registry_paths &&
            migrated->config.registry().system_path() == "D:\\system",
        "legacy default leaves migrate to one root");

    const auto custom =
        gc::config::ParseAndValidateInputConfigDocument(
            LegacyConfig(
                distributed,
                ".\\cabinet\\DUA\\news",
                ".\\cabinet\\DUA\\event",
                ".\\cabinet\\CmdFile\\log"));
    failures += Expect(
        custom && custom->migrations.registry_paths &&
            custom->config.registry().system_path() == ".\\cabinet",
        "consistent relative legacy leaves preserve their root");

    const auto mixed_case =
        gc::config::ParseAndValidateInputConfigDocument(
            LegacyConfig(
                distributed,
                "C:\\Root\\dUa\\NeWs",
                "c:/root/DUA/event",
                "C:\\ROOT\\cmdfile\\LOG"));
    failures += Expect(
        mixed_case && mixed_case->migrations.registry_paths &&
            mixed_case->config.registry().system_path() == "C:\\Root",
        "legacy Windows path components compare case-insensitively");

    const auto drive_root =
        gc::config::ParseAndValidateInputConfigDocument(
            LegacyConfig(
                distributed,
                "D:\\DUA\\news",
                "D:\\DUA\\event",
                "D:\\CmdFile\\log"));
    failures += Expect(
        drive_root && drive_root->migrations.registry_paths &&
            drive_root->config.registry().system_path() == "D:\\",
        "legacy leaves preserve a Windows drive root");

    failures += ExpectDocumentFailure(
        LegacyConfig(
            distributed,
            "N:\\news\\DUA\\news",
            "E:\\event\\DUA\\event",
            "L:\\log\\CmdFile\\log"),
        "do not share one system root",
        "unrelated legacy roots are rejected");

    failures += ExpectDocumentFailure(
        LegacyConfig(
            distributed,
            "D:\\system\\news",
            "D:\\system\\DUA\\event",
            "D:\\system\\CmdFile\\log"),
        "required Windows path components",
        "structurally invalid legacy leaf is rejected");

    const auto new_and_legacy = InsertAfterLine(
        distributed,
        "[registry.nesys]",
        "news_path = 'D:\\system\\DUA\\news'\n"
        "event_path = 'D:\\system\\DUA\\event'\n"
        "log_path = 'D:\\system\\CmdFile\\log'");
    failures += ExpectDocumentFailure(
        new_and_legacy,
        "both system_path and legacy",
        "new and legacy registry paths are ambiguous");

    const auto incomplete = RemoveAssignment(
        LegacyConfig(
            distributed,
            "D:\\system\\DUA\\news",
            "D:\\system\\DUA\\event",
            "D:\\system\\CmdFile\\log"),
        "event_path");
    failures += ExpectDocumentFailure(
        incomplete,
        "legacy registry paths must be complete",
        "partial legacy registry path set is rejected");

    const auto unknown_after_migration = InsertAfterLine(
        LegacyConfig(
            distributed,
            "D:\\system\\DUA\\news",
            "D:\\system\\DUA\\event",
            "D:\\system\\CmdFile\\log"),
        "[registry.nesys]",
        "mystery_path = 'D:\\mystery'");
    const auto unknown_result =
        gc::config::ParseAndValidateInputConfigDocument(
            unknown_after_migration);
    failures += Expect(
        !unknown_result,
        "unknown nonlegacy fields remain strict after migration");

    const InputConfig canonical_config = migrated
        ? migrated->config
        : InputConfig{};
    const std::filesystem::path target =
        L"X:\\cabinet\\config.toml";

    AtomicFake success_fake{};
    const auto success = gc::config::WriteInputConfigAtomically(
        target,
        canonical_config,
        AtomicActions(success_fake));
    failures += Expect(
        success && success_fake.writes == 1 &&
            success_fake.replaces == 1 && success_fake.removes == 0 &&
            success_fake.destination == target &&
            success_fake.temporary.parent_path() == target.parent_path() &&
            success_fake.serialized.find("system_path") !=
                std::string::npos &&
            success_fake.serialized.find("news_path") ==
                std::string::npos &&
            success_fake.serialized.find("audio_backend") !=
                std::string::npos &&
            success_fake.serialized.find(
                "enable_wasapi_exclusive_audio") ==
                std::string::npos,
        "atomic writer commits one canonical sibling replacement");

    AtomicFake write_failure{.fail_write = true};
    const auto failed_write = gc::config::WriteInputConfigAtomically(
        target,
        canonical_config,
        AtomicActions(write_failure));
    failures += Expect(
        !failed_write && write_failure.writes == 1 &&
            write_failure.replaces == 0 && write_failure.removes == 1 &&
            write_failure.removed == write_failure.temporary &&
            write_failure.temporary != success_fake.temporary &&
            failed_write.error().find("write") != std::string::npos &&
            failed_write.error().find("X:\\cabinet\\config.toml") !=
                std::string::npos,
        "atomic write failure cleans its temporary and names the target");

    AtomicFake replace_failure{.fail_replace = true};
    const auto failed_replace = gc::config::WriteInputConfigAtomically(
        target,
        canonical_config,
        AtomicActions(replace_failure));
    failures += Expect(
        !failed_replace && replace_failure.writes == 1 &&
            replace_failure.replaces == 1 &&
            replace_failure.removes == 1 &&
            replace_failure.removed == replace_failure.temporary &&
            replace_failure.temporary != success_fake.temporary &&
            replace_failure.temporary != write_failure.temporary &&
            failed_replace.error().find("replace") != std::string::npos &&
            failed_replace.error().find("X:\\cabinet\\config.toml") !=
                std::string::npos,
        "atomic replacement failure cleans its temporary and names the target");

    return failures == 0 ? 0 : 1;
}
