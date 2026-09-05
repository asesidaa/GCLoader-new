#include "Patches/AutoPlay/AutoPlayPatchDiagnostics.h"
#include "Diagnostics/FatalProcess.h"
namespace gc::auto_play {
[[noreturn]] void PublishAutoPlayMarkerRuntimeFatal() noexcept {
    try {
        diagnostics::AbortProcess({
            "AutoPlayPatch: mandatory marker rendering failed",
            L"GCLoader cannot continue playable auto play without its "
            L"mandatory visible marker. Check loader-log.txt for details.",
            L"GCLoader auto play marker failed"});
    } catch (...) { diagnostics::AbortProcess({}); }
}
}
