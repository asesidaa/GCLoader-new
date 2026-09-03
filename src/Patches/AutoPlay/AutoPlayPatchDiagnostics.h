#pragma once

#include "AutoPlayPatchTransaction.h"

#include <Windows.h>

#include <string>

namespace gc::auto_play
{
    struct AutoPlayFatalDiagnostic
    {
        std::string log;
        std::wstring modal;
        std::wstring title;
        DWORD exit_code{};
    };

    [[nodiscard]] AutoPlayFatalDiagnostic
    BuildAutoPlayPatchFatalDiagnostic(const AutoPlayPatchError& error);

    void PublishAutoPlaySetupFatal(
        const AutoPlayPatchError& error) noexcept;
    void PublishAutoPlaySetupFallbackFatal() noexcept;
    void PublishAutoPlayMarkerRuntimeFatal() noexcept;
} // namespace gc::auto_play
