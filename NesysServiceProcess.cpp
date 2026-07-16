#include "NesysServiceProcess.h"

#include <cctype>
#include <iterator>

namespace gc::nesys_service {
namespace {

std::string trim_ascii(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }

    return std::string{value.substr(first, last - first)};
}

std::string trim_token_quotes(std::string_view value) {
    auto trimmed = trim_ascii(value);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

std::string next_token(std::string_view command_line, std::size_t& offset) {
    while (offset < command_line.size() &&
           std::isspace(static_cast<unsigned char>(command_line[offset])) != 0) {
        ++offset;
    }

    if (offset >= command_line.size()) {
        return {};
    }

    if (command_line[offset] == '"') {
        const std::size_t start = offset;
        ++offset;
        while (offset < command_line.size() && command_line[offset] != '"') {
            ++offset;
        }
        if (offset < command_line.size()) {
            ++offset;
        }
        return std::string{command_line.substr(start, offset - start)};
    }

    const std::size_t start = offset;
    while (offset < command_line.size() &&
           std::isspace(static_cast<unsigned char>(command_line[offset])) == 0) {
        ++offset;
    }
    return std::string{command_line.substr(start, offset - start)};
}

std::string current_process_image_path() {
    std::string buffer(MAX_PATH, '\0');
    for (;;) {
        const DWORD copied = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return {};
        }

        if (copied < buffer.size() - 1) {
            buffer.resize(copied);
            return buffer;
        }

        buffer.resize(buffer.size() * 2);
    }
}

} // namespace

bool EqualsIgnoreCaseAscii(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto l = static_cast<unsigned char>(left[i]);
        const auto r = static_cast<unsigned char>(right[i]);
        if (std::tolower(l) != std::tolower(r)) {
            return false;
        }
    }

    return true;
}

std::string FileNameOfPathA(std::string_view path) {
    auto trimmed = trim_token_quotes(path);
    const auto separator = trimmed.find_last_of("\\/");
    if (separator == std::string::npos) {
        return trimmed;
    }
    return trimmed.substr(separator + 1);
}

std::string FirstCommandLineTokenA(std::string_view command_line) {
    std::size_t offset = 0;
    return trim_token_quotes(next_token(command_line, offset));
}

bool IsNesysServiceImagePathA(std::string_view image_path) {
    return EqualsIgnoreCaseAscii(FileNameOfPathA(image_path), "NesysService.exe");
}

bool CommandLineContainsAppArgumentA(std::string_view command_line) {
    std::size_t offset = 0;
    while (offset < command_line.size()) {
        const auto token = trim_token_quotes(next_token(command_line, offset));
        if (token.empty()) {
            continue;
        }
        if (EqualsIgnoreCaseAscii(token, "-app")) {
            return true;
        }
    }
    return false;
}

bool IsNesysServiceLaunchA(LPCSTR application_name, LPSTR command_line) {
    const std::string_view app = application_name != nullptr ? std::string_view{application_name} : std::string_view{};
    const std::string_view cmd = command_line != nullptr ? std::string_view{command_line} : std::string_view{};

    if (!CommandLineContainsAppArgumentA(cmd)) {
        return false;
    }

    if (!app.empty()) {
        return IsNesysServiceImagePathA(app);
    }

    return IsNesysServiceImagePathA(FirstCommandLineTokenA(cmd));
}

ProcessRole DetectProcessRoleFromImagePathA(std::string_view image_path) {
    return IsNesysServiceImagePathA(image_path) ? ProcessRole::Service : ProcessRole::Game;
}

ProcessRole DetectCurrentProcessRole() {
    const auto image_path = current_process_image_path();
    if (image_path.empty()) {
        return ProcessRole::Game;
    }
    return DetectProcessRoleFromImagePathA(image_path);
}

bool ShouldRunGameOnlyInitialization(ProcessRole role) {
    return role == ProcessRole::Game;
}

const char* ProcessRoleName(ProcessRole role) {
    switch (role) {
    case ProcessRole::Game:
        return "game";
    case ProcessRole::Service:
        return "service";
    }
    return "unknown";
}

DWORD AddCreateSuspendedFlag(DWORD creation_flags) {
    return creation_flags | CREATE_SUSPENDED;
}

bool WasCreateSuspendedRequested(DWORD creation_flags) {
    return (creation_flags & CREATE_SUSPENDED) != 0;
}

bool ShouldResumeAfterServiceInjection(bool caller_requested_suspended) {
    return !caller_requested_suspended;
}

NesysFeaturePlan ResolveNesysFeaturePlan(
    ProcessRole role,
    bool network_enabled,
    bool registry_enabled) noexcept {
    NesysFeaturePlan plan{};
    plan.enabled = network_enabled || registry_enabled;
    plan.network_virtualization = network_enabled;
    plan.registry_virtualization = registry_enabled;

    if (network_enabled) {
        plan.synthetic_adapter = true;
        plan.server_address_override = true;
        if (role == ProcessRole::Game) {
            plan.api_hook_count += 5;
        } else {
            plan.api_hook_count += 10;
            plan.service_ping_redirect = true;
        }
    }

    if (registry_enabled) {
        plan.registry_config_override = true;
        plan.api_hook_count += 3;
    }

    if (plan.enabled) {
        if (role == ProcessRole::Game) {
            plan.service_launcher = true;
        }
        ++plan.api_hook_count;
    }

    return plan;
}

} // namespace gc::nesys_service
