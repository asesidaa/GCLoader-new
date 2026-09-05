#pragma once
#include "Platform/Win32/Hooking/HookIdentity.h"
#include <Windows.h>
namespace gc::hooking {
enum class HookStage : std::uint8_t {
    invalid_plan, resolve_module, resolve_export, collision, create, publish_original, enable,
};
struct HookError final {
    HookStage stage{};
    HookIdentity identity{};
    std::uintptr_t address{};
    std::optional<ExportTarget> export_target;
    HookIdentity collision_peer{};
    DWORD win32_error{};
    std::uint32_t safetyhook_error{};
    std::optional<std::uint32_t> inline_error;
};
}
