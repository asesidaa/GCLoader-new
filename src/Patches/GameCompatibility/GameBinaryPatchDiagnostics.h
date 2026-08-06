#pragma once

#include "Patches/GameCompatibility/GameBinaryPatch.h"

#include <string>

namespace gc::game_compatibility {

struct GameBinaryPatchFatalDiagnostic {
    std::string log;
    std::wstring modal;
    std::wstring title;
    DWORD exit_code{26};
};

[[nodiscard]] GameBinaryPatchFatalDiagnostic
BuildGameBinaryPatchFatalDiagnostic(
    const GameBinaryPatchError& error);

} // namespace gc::game_compatibility
