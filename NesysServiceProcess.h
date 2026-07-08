#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

namespace gc::nesys_service {

enum class ProcessRole {
    Game,
    Service,
};

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
