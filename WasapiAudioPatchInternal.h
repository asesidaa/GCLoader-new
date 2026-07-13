#pragma once

#include "WasapiAudioPatch.h"

namespace gc::audio::detail {

struct AudioResolverApi {
    decltype(&GetModuleHandleW) get_module_handle{};
    decltype(&GetProcAddress) get_proc_address{};
};

bool InstallWasapiAudioHookWithResolver(
    bool enabled,
    AudioMinHookApi minhook,
    AudioResolverApi resolver,
    AudioHookFailure* failure) noexcept;

} // namespace gc::audio::detail
