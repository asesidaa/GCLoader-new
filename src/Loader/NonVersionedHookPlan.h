#pragma once
#include "Loader/StartupFailure.h"
namespace gc::audio { struct PreparedAudioFeature; }
namespace gc::loader {
[[nodiscard]] std::expected<hooking::ValidatedHookPlan, StartupError>
PrepareGameNonVersionedHooks(HMODULE loader_module, const config::ValidatedConfig&,
    const system_path::RuntimeRoot&, const audio::PreparedAudioFeature&) noexcept;
}
