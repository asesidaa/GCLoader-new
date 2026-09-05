#pragma once
#include "Platform/Win32/Hooking/HookPlan.h"

namespace gc::nesys_service::diagnostics {
[[nodiscard]] std::expected<void, hooking::HookError> AddServiceRequestPipelineHooks(hooking::HookPlan&) noexcept;
}
