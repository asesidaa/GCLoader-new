#pragma once

#include <cstdint>

namespace gc::auto_play
{
    using NativeDebugTextFunction = int(__cdecl*)(
        float,
        float,
        std::uint32_t,
        const char*,
        ...);

    [[nodiscard]] bool DrawAutoPlayMarker(
        NativeDebugTextFunction function) noexcept;
} // namespace gc::auto_play
