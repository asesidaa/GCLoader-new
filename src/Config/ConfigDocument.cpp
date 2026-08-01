#include "Config/ConfigDocument.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#include "rfl/toml.hpp"
#include <toml++/toml.hpp>

namespace gc::config {

namespace {

constexpr std::string_view kObsoleteFramerateBoolean =
    "enable_120fps_" "timer_patches";

constexpr std::string_view kObsoleteInputSchemaError =
    "obsolete SDL input schema is not supported; remove gamepad_index, "
    "axis_threshold, and [gamepad]";

std::expected<toml::table, std::string> ParseTomlSyntax(
    std::string_view text) {
#if TOML_EXCEPTIONS
    try {
        return toml::parse(text);
    } catch (const toml::parse_error& error) {
        return std::unexpected(
            "Failed to parse config file: " +
            std::string{error.description()});
    }
#else
    auto result = toml::parse(text);
    if (!result) {
        return std::unexpected(
            "Failed to parse config file: " +
            std::string{result.error().description()});
    }
    return std::move(result).table();
#endif
}

std::expected<void, std::string> ValidateObsoleteSyntax(
    const toml::table& syntax) {
    if (syntax.contains("gamepad_index") ||
        syntax.contains("axis_threshold") ||
        syntax.contains("gamepad")) {
        return std::unexpected(std::string{kObsoleteInputSchemaError});
    }

    const auto* schema_version =
        syntax["input_schema_version"].as_integer();
    if (schema_version == nullptr) {
        return std::unexpected(
            "Missing or non-integer input_schema_version; expected 2");
    }
    if (schema_version->get() != kInputSchemaVersion) {
        return std::unexpected(
            "Unsupported input_schema_version; expected 2");
    }

    if (const auto* experimental = syntax["experimental"].as_table();
        experimental != nullptr &&
        experimental->contains(kObsoleteFramerateBoolean)) {
        return std::unexpected(
            "Obsolete [experimental].enable_120fps_"
            "timer_patches is not supported; replace it with target_fps = 60 through 500");
    }
    return {};
}

constexpr bool IsSeparator(char value) noexcept {
    return value == '\\' || value == '/';
}

constexpr char FoldWindowsCharacter(char value) noexcept {
    if (IsSeparator(value)) {
        return '\\';
    }
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

bool WindowsPathTextEqual(
    std::string_view left,
    std::string_view right) noexcept {
    while (left.size() > 1 && IsSeparator(left.back())) {
        left.remove_suffix(1);
    }
    while (right.size() > 1 && IsSeparator(right.back())) {
        right.remove_suffix(1);
    }
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (FoldWindowsCharacter(left[index]) !=
            FoldWindowsCharacter(right[index])) {
            return false;
        }
    }
    return true;
}

std::expected<std::wstring, std::string> NormalizedWindowsRoot(
    std::string_view value) {
    if (value.empty() ||
        value.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return std::unexpected(
            "legacy registry path root is not valid UTF-8 path text");
    }
    const auto source_size = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        source_size,
        nullptr,
        0);
    if (required <= 0) {
        return std::unexpected(
            "legacy registry path root is not valid UTF-8 path text");
    }

    std::wstring normalized(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            source_size,
            normalized.data(),
            required) != required) {
        return std::unexpected(
            "legacy registry path root could not be decoded from UTF-8");
    }
    for (wchar_t& character : normalized) {
        if (character == L'/') {
            character = L'\\';
        }
    }
    while (normalized.size() > 1 && normalized.back() == L'\\') {
        normalized.pop_back();
    }
    return normalized;
}

std::expected<bool, std::string> WindowsRootsEqual(
    std::string_view left,
    std::string_view right) {
    auto normalized_left = NormalizedWindowsRoot(left);
    if (!normalized_left) {
        return std::unexpected(normalized_left.error());
    }
    auto normalized_right = NormalizedWindowsRoot(right);
    if (!normalized_right) {
        return std::unexpected(normalized_right.error());
    }
    const int comparison = CompareStringOrdinal(
        normalized_left->data(),
        static_cast<int>(normalized_left->size()),
        normalized_right->data(),
        static_cast<int>(normalized_right->size()),
        TRUE);
    if (comparison == 0) {
        return std::unexpected(
            "legacy registry path roots could not be compared with "
            "Windows path semantics");
    }
    return comparison == CSTR_EQUAL;
}

std::expected<std::string, std::string> StripLegacySuffix(
    std::string_view path,
    std::string_view parent,
    std::string_view leaf,
    std::string_view field_name) {
    const auto leaf_separator = path.find_last_of("\\/");
    if (leaf_separator == std::string_view::npos ||
        leaf_separator + 1 >= path.size() ||
        !WindowsPathTextEqual(
            path.substr(leaf_separator + 1),
            leaf)) {
        return std::unexpected(
            "legacy " + std::string{field_name} +
            " does not end in the required Windows path components");
    }

    const auto parent_separator = path.find_last_of(
        "\\/",
        leaf_separator - 1);
    if (parent_separator == std::string_view::npos ||
        parent_separator + 1 >= leaf_separator ||
        !WindowsPathTextEqual(
            path.substr(
                parent_separator + 1,
                leaf_separator - parent_separator - 1),
            parent)) {
        return std::unexpected(
            "legacy " + std::string{field_name} +
            " does not end in the required Windows path components");
    }

    std::string root{path.substr(0, parent_separator)};
    if (root.empty() && parent_separator == 0) {
        root.assign(1, path.front());
    } else if (root.size() == 2 && root[1] == ':') {
        root.push_back(path[parent_separator]);
    }
    if (root.empty()) {
        return std::unexpected(
            "legacy " + std::string{field_name} +
            " has no system root");
    }
    return root;
}

std::expected<bool, std::string> MigrateLegacyRegistryPaths(
    toml::table& syntax) {
    auto* registry = syntax["registry"].as_table();
    if (registry == nullptr) {
        return false;
    }
    auto* nesys = (*registry)["nesys"].as_table();
    if (nesys == nullptr) {
        return false;
    }

    const bool has_news = nesys->contains("news_path");
    const bool has_event = nesys->contains("event_path");
    const bool has_log = nesys->contains("log_path");
    const bool has_legacy = has_news || has_event || has_log;
    if (!has_legacy) {
        return false;
    }
    if (registry->contains("system_path")) {
        return std::unexpected(
            "config contains both system_path and legacy registry paths");
    }
    if (!(has_news && has_event && has_log)) {
        return std::unexpected(
            "legacy registry paths must be complete: news_path, "
            "event_path, and log_path are all required");
    }

    const auto news_value = (*nesys)["news_path"].value<std::string>();
    const auto event_value = (*nesys)["event_path"].value<std::string>();
    const auto log_value = (*nesys)["log_path"].value<std::string>();
    if (!news_value || !event_value || !log_value) {
        return std::unexpected(
            "legacy registry paths must be complete string values");
    }

    auto news_root = StripLegacySuffix(
        *news_value,
        "DUA",
        "news",
        "news_path");
    if (!news_root) {
        return std::unexpected(news_root.error());
    }
    auto event_root = StripLegacySuffix(
        *event_value,
        "DUA",
        "event",
        "event_path");
    if (!event_root) {
        return std::unexpected(event_root.error());
    }
    auto log_root = StripLegacySuffix(
        *log_value,
        "CmdFile",
        "log",
        "log_path");
    if (!log_root) {
        return std::unexpected(log_root.error());
    }

    auto news_matches_event = WindowsRootsEqual(
        *news_root,
        *event_root);
    if (!news_matches_event) {
        return std::unexpected(news_matches_event.error());
    }
    auto news_matches_log = WindowsRootsEqual(*news_root, *log_root);
    if (!news_matches_log) {
        return std::unexpected(news_matches_log.error());
    }
    if (!*news_matches_event || !*news_matches_log) {
        return std::unexpected(
            "legacy registry paths do not share one system root");
    }

    registry->insert_or_assign("system_path", *news_root);
    nesys->erase("news_path");
    nesys->erase("event_path");
    nesys->erase("log_path");
    return true;
}

std::string PathForDiagnostic(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {
        reinterpret_cast<const char*>(utf8.data()),
        utf8.size(),
    };
}

std::filesystem::path MakeTemporaryPath(
    const std::filesystem::path& config_path) {
    static std::atomic_uint64_t sequence{};
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = now.dwLowDateTime;
    ticks.HighPart = now.dwHighDateTime;

    std::wstring name = config_path.filename().native();
    name += L".gcloader.";
    name += std::to_wstring(GetCurrentProcessId());
    name += L".";
    name += std::to_wstring(ticks.QuadPart);
    name += L".";
    name += std::to_wstring(
        sequence.fetch_add(1, std::memory_order_relaxed));
    name += L".tmp";
    return config_path.parent_path() / name;
}

std::expected<void, std::string> ProductionWrite(
    void*,
    const std::filesystem::path& path,
    std::string_view serialized) noexcept {
    try {
        std::ofstream output{
            path,
            std::ios::binary | std::ios::trunc};
        if (!output) {
            return std::unexpected("could not open temporary file");
        }
        output.write(
            serialized.data(),
            static_cast<std::streamsize>(serialized.size()));
        if (!output) {
            return std::unexpected("could not write temporary file");
        }
        output.flush();
        if (!output) {
            return std::unexpected("could not flush temporary file");
        }
        output.close();
        if (output.fail()) {
            return std::unexpected("could not close temporary file");
        }
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    } catch (...) {
        return std::unexpected("unexpected temporary-file write failure");
    }
}

std::expected<void, std::string> ProductionReplace(
    void*,
    const std::filesystem::path& destination,
    const std::filesystem::path& replacement) noexcept {
    try {
        if (ReplaceFileW(
                destination.c_str(),
                replacement.c_str(),
                nullptr,
                0,
                nullptr,
                nullptr) != FALSE) {
            return {};
        }
        return std::unexpected(
            "ReplaceFileW failed with Win32 error " +
            std::to_string(GetLastError()));
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    } catch (...) {
        return std::unexpected("unexpected atomic replacement failure");
    }
}

void ProductionRemove(
    void*,
    const std::filesystem::path& path) noexcept {
    DeleteFileW(path.c_str());
}

} // namespace

std::expected<ParsedInputConfigDocument, std::string>
ParseAndValidateInputConfigDocument(std::string_view text) {
    auto syntax = ParseTomlSyntax(text);
    if (!syntax) {
        return std::unexpected(syntax.error());
    }
    if (auto obsolete = ValidateObsoleteSyntax(*syntax); !obsolete) {
        return std::unexpected(obsolete.error());
    }
    auto migration = MigrateLegacyRegistryPaths(*syntax);
    if (!migration) {
        return std::unexpected(migration.error());
    }

    std::ostringstream canonical_text;
    canonical_text << *syntax;
    auto parsed = rfl::toml::read<InputConfig, rfl::NoExtraFields>(
        canonical_text.str());
    if (!parsed) {
        return std::unexpected(
            "Failed to parse config file: " + parsed.error().what());
    }
    if (auto validation = ValidateInputConfig(parsed.value());
        !validation) {
        return std::unexpected(validation.error());
    }
    return ParsedInputConfigDocument{
        .config = std::move(parsed.value()),
        .registry_paths_migrated = *migration,
    };
}

AtomicConfigWriteActions ProductionAtomicConfigWriteActions() noexcept {
    return {
        .write = &ProductionWrite,
        .replace = &ProductionReplace,
        .remove = &ProductionRemove,
    };
}

std::expected<void, std::string> WriteInputConfigAtomically(
    const std::filesystem::path& config_path,
    const InputConfig& config,
    AtomicConfigWriteActions actions) noexcept {
    bool temporary_may_exist = false;
    std::filesystem::path temporary;
    try {
        const std::string target = PathForDiagnostic(config_path);
        if (config_path.empty()) {
            return std::unexpected(
                "Config write target path must not be empty");
        }
        if (actions.write == nullptr ||
            actions.replace == nullptr ||
            actions.remove == nullptr) {
            return std::unexpected(
                "Config write actions are incomplete for '" +
                target + "'");
        }

        const std::string serialized = rfl::toml::write(config);
        temporary = MakeTemporaryPath(config_path);
        temporary_may_exist = true;
        auto write_result = actions.write(
            actions.context,
            temporary,
            serialized);
        if (!write_result) {
            actions.remove(actions.context, temporary);
            temporary_may_exist = false;
            return std::unexpected(
                "Config write stage failed for '" + target +
                "': " + write_result.error());
        }

        auto replace_result = actions.replace(
            actions.context,
            config_path,
            temporary);
        if (!replace_result) {
            actions.remove(actions.context, temporary);
            temporary_may_exist = false;
            return std::unexpected(
                "Config replace stage failed for '" + target +
                "': " + replace_result.error());
        }
        temporary_may_exist = false;
        return {};
    } catch (const std::exception& error) {
        if (temporary_may_exist && actions.remove != nullptr) {
            actions.remove(actions.context, temporary);
        }
        return std::unexpected(
            "Config persistence failed: " + std::string{error.what()});
    } catch (...) {
        if (temporary_may_exist && actions.remove != nullptr) {
            actions.remove(actions.context, temporary);
        }
        return std::unexpected("Config persistence failed unexpectedly");
    }
}

} // namespace gc::config
