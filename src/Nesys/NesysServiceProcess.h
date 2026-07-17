#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace gc::nesys_service {

enum class ProcessRole {
    Game,
    Service,
};

struct NesysFeaturePlan {
    bool enabled{false};
    bool network_virtualization{false};
    bool registry_virtualization{false};
    bool synthetic_adapter{false};
    bool server_address_override{false};
    bool registry_config_override{false};
    bool service_launcher{false};
    bool service_ping_redirect{false};
    std::size_t api_hook_count{0};

    bool operator==(const NesysFeaturePlan&) const = default;
};

NesysFeaturePlan ResolveNesysFeaturePlan(
    ProcessRole role,
    bool network_enabled,
    bool registry_enabled) noexcept;

bool EqualsIgnoreCaseAscii(std::string_view left, std::string_view right);
std::string FileNameOfPathA(std::string_view path);
std::string FirstCommandLineTokenA(std::string_view command_line);
bool IsNesysServiceImagePathA(std::string_view image_path);
bool CommandLineContainsAppArgumentA(std::string_view command_line);
bool IsNesysServiceLaunchA(LPCSTR application_name, LPSTR command_line);
ProcessRole DetectProcessRoleFromImagePathA(std::string_view image_path);
ProcessRole DetectCurrentProcessRole();
bool ShouldRunGameOnlyInitialization(ProcessRole role);
const char* ProcessRoleName(ProcessRole role);
DWORD AddCreateSuspendedFlag(DWORD creation_flags);
bool WasCreateSuspendedRequested(DWORD creation_flags);
bool ShouldResumeAfterServiceInjection(bool caller_requested_suspended);

} // namespace gc::nesys_service
