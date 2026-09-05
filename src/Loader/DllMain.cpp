#include <WinSock2.h>
#include <Windows.h>
#include "Loader/ProcessStartup.h"
#include "Loader/GameStartup.h"
#include "Loader/NesysStartup.h"

#ifndef _M_IX86
#error "Only Win32 version is supported!"
#endif

BOOL APIENTRY DllMain(HMODULE loader_module, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    try {
        (void)DisableThreadLibraryCalls(loader_module);
        auto configuration = gc::loader::PrepareProcessStartup(loader_module);
        if (!configuration) gc::loader::AbortForStartupError(configuration.error());
        auto started = std::visit([&](auto&& value) -> std::expected<void, gc::loader::StartupError> {
            using Configuration = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Configuration, gc::loader::GameProcessConfiguration>)
                return gc::loader::StartGame(loader_module, std::move(value));
            else
                return gc::loader::StartNesys(loader_module, std::move(value));
        }, std::move(*configuration));
        if (!started) gc::loader::AbortForStartupError(started.error());
        return TRUE;
    } catch (...) {
        gc::loader::AbortForStartupError({.stage = gc::loader::StartupStage::exception});
    }
}
